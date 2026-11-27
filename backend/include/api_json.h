#ifndef TRANSPORT_API_JSON_H
#define TRANSPORT_API_JSON_H

/**
 * @file api_json.h
 * @brief Pure JSON response builders, request parsers, strict input validators, and HTTP error mapping.
 *
 * Caller must hold app_lock(ctx) while invoking handlers/builders that inspect or mutate AppCtx state
 * (api_json_eval only accesses the configured eval path).
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

Status api_parse_int_strict(const char *s, int min_val, int max_val, int *out);
Status api_parse_double_strict(const char *s, double min_val, double max_val, double *out);
int api_http_status_from_error(Status st, const char **code_out);
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
 * @brief Reads data/eval/summary.json at request time and validates JSON.
 * @return STATUS_OK, STATUS_ERR_NOT_FOUND (404), or STATUS_ERR_NULL (500 on invalid JSON).
 * @note Complexity: O(file_bytes).
 */
Status api_json_eval(const AppCtx *ctx, char **out);

Status api_json_sim_control_parse(const char *body, char *action_buf, size_t action_cap,
                                  int *has_action, int *has_speed, double *speed_out);
Status api_json_sim_control_parse_ext(const char *body, char *action_buf, size_t action_cap,
                                      int *has_action, int *has_speed, double *speed_out,
                                      int *has_time_min, double *time_min_out);
Status api_json_sim_control_ok(const AppCtx *ctx, char **out);

Status api_json_handle_scenario(AppCtx *ctx, const char *body, char **out);
Status api_json_handle_allocate(AppCtx *ctx, const char *body, char **out);
Status api_json_handle_inject(AppCtx *ctx, const char *body, char **out);
Status api_json_handle_route_optimize(AppCtx *ctx, const char *body, char **out);
Status api_json_handle_undo(AppCtx *ctx, const char *body, char **out);

#ifdef __cplusplus
}
#endif

#endif /* TRANSPORT_API_JSON_H */
