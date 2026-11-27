#include "api_http.h"
#include "api_json.h"

#include <civetweb.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define API_MAX_BODY_BYTES 65536

struct ApiHttpServer {
    AppCtx *app;
    struct mg_context *mg;
    int mg_initialized;
};

static const char *http_reason(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500:
        default:  return "Internal Server Error";
    }
}

static void send_json_response(struct mg_connection *conn, int status, const char *origin,
                               const char *allow_hdr, const char *body) {
    size_t len = body ? strlen(body) : 0U;
    mg_printf(conn,
              "HTTP/1.1 %d %s\r\n"
              "Content-Type: application/json\r\n"
              "Cache-Control: no-store\r\n"
              "Content-Length: %zu\r\n"
              "Access-Control-Allow-Origin: %s\r\n"
              "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
              "Access-Control-Allow-Headers: Content-Type\r\n"
              "Access-Control-Max-Age: 600\r\n",
              status, http_reason(status), len,
              origin ? origin : "http://localhost:5173");
    if (allow_hdr && allow_hdr[0] != '\0') {
        mg_printf(conn, "Allow: %s\r\n", allow_hdr);
    }
    mg_write(conn, "\r\n", 2);
    if (body && len > 0U) {
        mg_write(conn, body, len);
    }
}

static int send_error_response(struct mg_connection *conn, int status, const char *origin,
                               const char *allow_hdr, const char *code, const char *msg) {
    char *err_json = api_json_error(code, msg);
    if (!err_json) {
        static const char fallback[] = "{\"error\":{\"code\":\"internal\",\"message\":\"out of memory\"}}";
        send_json_response(conn, 500, origin, allow_hdr, fallback);
        return 500;
    }
    send_json_response(conn, status, origin, allow_hdr, err_json);
    free(err_json);
    return status;
}

static int send_status_error(struct mg_connection *conn, Status st, const char *origin,
                             const char *msg) {
    const char *code = NULL;
    int http = api_http_status_from_error(st, &code);
    return send_error_response(conn, http, origin, NULL, code, msg);
}

/* Returns 1 if found and non-empty, 0 if absent or empty, -1 if buffer overflow */
static int query_param_get(const char *qs, const char *name, char *buf, size_t cap) {
    if (!buf || cap == 0) return -1;
    buf[0] = '\0';
    if (!qs || qs[0] == '\0') return 0;

    int rc = mg_get_var2(qs, strlen(qs), name, buf, cap, 0);
    if (rc == -1) return 0;
    if (rc == -2) return -1;
    if (rc == 0 || buf[0] == '\0') return 0;
    return 1;
}

static int read_request_body(struct mg_connection *conn, const struct mg_request_info *ri,
                             char **out_body, int *too_large) {
    *out_body = NULL;
    *too_large = 0;

    if (ri->content_length > (long long)API_MAX_BODY_BYTES) {
        *too_large = 1;
        return 0;
    }

    size_t cap = (ri->content_length >= 0) ? (size_t)ri->content_length + 1U : 4096U;
    if (cap > (size_t)API_MAX_BODY_BYTES + 2U) {
        cap = (size_t)API_MAX_BODY_BYTES + 2U;
    }
    char *buf = (char *)malloc(cap);
    if (!buf) return -1;

    size_t total = 0U;
    for (;;) {
        if (total + 1U >= cap) {
            if (cap >= (size_t)API_MAX_BODY_BYTES + 2U) {
                /* Probe one extra byte to detect bodies > 64 KB */
                char extra = 0;
                int n = mg_read(conn, &extra, 1);
                if (n > 0) {
                    free(buf);
                    *too_large = 1;
                    return 0;
                }
                break;
            }
            size_t ncap = cap * 2U;
            if (ncap > (size_t)API_MAX_BODY_BYTES + 2U) {
                ncap = (size_t)API_MAX_BODY_BYTES + 2U;
            }
            char *nb = (char *)realloc(buf, ncap);
            if (!nb) {
                free(buf);
                return -1;
            }
            buf = nb;
            cap = ncap;
        }
        int n = mg_read(conn, buf + total, cap - total - 1U);
        if (n <= 0) break;
        total += (size_t)n;
        if (total > (size_t)API_MAX_BODY_BYTES) {
            free(buf);
            *too_large = 1;
            return 0;
        }
    }

    buf[total] = '\0';
    *out_body = buf;
    return 1;
}

static int api_request_handler(struct mg_connection *conn, void *cbdata) {
    ApiHttpServer *srv = (ApiHttpServer *)cbdata;
    if (!srv || !srv->app || !conn) return 500;

    AppCtx *app = srv->app;
    const struct mg_request_info *ri = mg_get_request_info(conn);
    if (!ri || !ri->request_method || !ri->local_uri) return 500;

    const char *method = ri->request_method;
    const char *uri = ri->local_uri;
    const char *origin = app_cors_origin(app);

    if (strcmp(method, "OPTIONS") == 0) {
        send_json_response(conn, 204, origin, NULL, NULL);
        return 204;
    }

    int is_get = (strcmp(method, "GET") == 0);
    int is_post = (strcmp(method, "POST") == 0);

    /* Route POST endpoint */
    if (strcmp(uri, "/api/sim/control") == 0) {
        if (!is_post) {
            return send_error_response(conn, 405, origin, "POST, OPTIONS",
                                       "method_not_allowed", "POST required");
        }
        char *body = NULL;
        int too_large = 0;
        int rrc = read_request_body(conn, ri, &body, &too_large);
        if (too_large) {
            return send_error_response(conn, 413, origin, NULL,
                                       "payload_too_large", "request body exceeds 64 KB limit");
        }
        if (rrc < 0 || !body) {
            return send_error_response(conn, 500, origin, NULL,
                                       "internal", "failed reading request body");
        }

        char action[32];
        int has_action = 0;
        int has_speed = 0;
        double speed = 0.0;
        Status st = api_json_sim_control_parse(body, action, sizeof(action),
                                                &has_action, &has_speed, &speed);
        free(body);
        if (st != STATUS_OK) {
            return send_status_error(conn, st, origin, "invalid control payload");
        }

        char *resp = NULL;
        if (app_lock(app) != STATUS_OK) {
            return send_error_response(conn, 500, origin, NULL, "internal", "lock failed");
        }
        st = app_sim_control(app, has_action ? action : NULL, has_speed, speed);
        if (st == STATUS_OK) {
            st = api_json_sim_control_ok(app, &resp);
        }
        app_unlock(app);

        if (st != STATUS_OK || !resp) {
            free(resp);
            return send_status_error(conn, st, origin, "control update failed");
        }
        send_json_response(conn, 200, origin, NULL, resp);
        free(resp);
        return 200;
    }

    /* Check GET endpoints */
    typedef enum EndpointKind {
        EP_NONE = 0,
        EP_HEALTH,
        EP_CONFIG,
        EP_GRAPH,
        EP_STOPS,
        EP_ROUTES,
        EP_GEOMETRY,
        EP_STATE,
        EP_BUS,
        EP_SCHEDULE,
        EP_TREE,
        EP_FORECAST,
        EP_METRICS,
        EP_ALERTS,
        EP_HISTORY
    } EndpointKind;

    EndpointKind ep = EP_NONE;
    int bus_id = -1;

    if (strcmp(uri, "/api/health") == 0) ep = EP_HEALTH;
    else if (strcmp(uri, "/api/config") == 0) ep = EP_CONFIG;
    else if (strcmp(uri, "/api/graph") == 0) ep = EP_GRAPH;
    else if (strcmp(uri, "/api/stops") == 0) ep = EP_STOPS;
    else if (strcmp(uri, "/api/routes") == 0) ep = EP_ROUTES;
    else if (strcmp(uri, "/api/geometry") == 0) ep = EP_GEOMETRY;
    else if (strcmp(uri, "/api/state") == 0) ep = EP_STATE;
    else if (strncmp(uri, "/api/bus/", 9) == 0) ep = EP_BUS;
    else if (strcmp(uri, "/api/schedule") == 0) ep = EP_SCHEDULE;
    else if (strcmp(uri, "/api/tree") == 0) ep = EP_TREE;
    else if (strcmp(uri, "/api/demand/forecast") == 0) ep = EP_FORECAST;
    else if (strcmp(uri, "/api/metrics") == 0) ep = EP_METRICS;
    else if (strcmp(uri, "/api/alerts") == 0) ep = EP_ALERTS;
    else if (strcmp(uri, "/api/history") == 0) ep = EP_HISTORY;

    if (ep == EP_NONE) {
        return send_error_response(conn, 404, origin, NULL,
                                   "not_found", "unknown endpoint");
    }
    if (!is_get) {
        return send_error_response(conn, 405, origin, "GET, OPTIONS",
                                   "method_not_allowed", "GET required");
    }

    /* Parse query / path parameters before locking */
    int has_from = 0, from_min = 0;
    int has_to = 0, to_min = 0;
    int has_route = 0, route_id = -1;
    int forecast_stop = -1;

    if (ep == EP_BUS) {
        const char *id_str = uri + 9;
        if (id_str[0] == '\0' ||
            api_parse_int_strict(id_str, 0, INT_MAX, &bus_id) != STATUS_OK) {
            return send_error_response(conn, 400, origin, NULL,
                                       "invalid_argument", "invalid bus id");
        }
    } else if (ep == EP_SCHEDULE) {
        char qbuf[64];
        int p = query_param_get(ri->query_string, "from", qbuf, sizeof(qbuf));
        if (p < 0 || (p > 0 && api_parse_int_strict(qbuf, 0, 1440, &from_min) != STATUS_OK)) {
            return send_error_response(conn, 400, origin, NULL,
                                       "invalid_argument", "invalid 'from' parameter");
        }
        has_from = p;

        p = query_param_get(ri->query_string, "to", qbuf, sizeof(qbuf));
        if (p < 0 || (p > 0 && api_parse_int_strict(qbuf, 0, 1440, &to_min) != STATUS_OK)) {
            return send_error_response(conn, 400, origin, NULL,
                                       "invalid_argument", "invalid 'to' parameter");
        }
        has_to = p;

        p = query_param_get(ri->query_string, "route", qbuf, sizeof(qbuf));
        if (p < 0 || (p > 0 && api_parse_int_strict(qbuf, -1, 100000, &route_id) != STATUS_OK)) {
            return send_error_response(conn, 400, origin, NULL,
                                       "invalid_argument", "invalid 'route' parameter");
        }
        has_route = p;
    } else if (ep == EP_FORECAST) {
        char qbuf[64];
        int p = query_param_get(ri->query_string, "stop", qbuf, sizeof(qbuf));
        if (p <= 0 || api_parse_int_strict(qbuf, 0, INT_MAX, &forecast_stop) != STATUS_OK) {
            return send_error_response(conn, 400, origin, NULL,
                                       "invalid_argument", "invalid or missing 'stop' parameter");
        }
    }

    char *resp = NULL;
    Status st = STATUS_ERR_INVALID;
    if (app_lock(app) != STATUS_OK) {
        return send_error_response(conn, 500, origin, NULL, "internal", "lock failed");
    }
    switch (ep) {
        case EP_HEALTH:   st = api_json_health(app, &resp); break;
        case EP_CONFIG:   st = api_json_config(app, &resp); break;
        case EP_GRAPH:    st = api_json_graph(app, &resp); break;
        case EP_STOPS:    st = api_json_stops(app, &resp); break;
        case EP_ROUTES:   st = api_json_routes(app, &resp); break;
        case EP_GEOMETRY: st = api_json_geometry(app, &resp); break;
        case EP_STATE:    st = api_json_state(app, &resp); break;
        case EP_BUS:      st = api_json_bus(app, bus_id, &resp); break;
        case EP_SCHEDULE: st = api_json_schedule(app, has_from, from_min,
                                                 has_to, to_min, has_route, route_id, &resp); break;
        case EP_TREE:     st = api_json_tree(app, &resp); break;
        case EP_FORECAST: st = api_json_forecast(app, forecast_stop, &resp); break;
        case EP_METRICS:  st = api_json_metrics(app, &resp); break;
        case EP_ALERTS:   st = api_json_alerts(app, &resp); break;
        case EP_HISTORY:  st = api_json_history(app, &resp); break;
        case EP_NONE:
        default:          st = STATUS_ERR_NOT_FOUND; break;
    }
    app_unlock(app);

    if (st != STATUS_OK || !resp) {
        free(resp);
        return send_status_error(conn, st, origin, "request failed");
    }

    send_json_response(conn, 200, origin, NULL, resp);
    free(resp);
    return 200;
}

static int mg_log_message_cb(const struct mg_connection *conn, const char *message) {
    (void)conn;
    if (message && message[0] != '\0') {
        fprintf(stderr, "[civetweb] %s\n", message);
    }
    return 1;
}

Status api_http_start(AppCtx *app, ApiHttpServer **out) {
    if (!app || !out) return STATUS_ERR_NULL;
    *out = NULL;

    const AppOptions *opt = app_options(app);
    if (!opt || !opt->bind_addr) return STATUS_ERR_INVALID;

    ApiHttpServer *srv = (ApiHttpServer *)calloc(1, sizeof(ApiHttpServer));
    if (!srv) return STATUS_ERR_NOMEM;
    srv->app = app;

    /* MG_FEATURES_DEFAULT (0) returns 0 when no TLS/optional features are requested */
    (void)mg_init_library(MG_FEATURES_DEFAULT);
    srv->mg_initialized = 1;

    char listen_buf[128];
    snprintf(listen_buf, sizeof(listen_buf), "%s:%d", opt->bind_addr, opt->port);

    const char *mg_opts[] = {
        "listening_ports", listen_buf,
        "num_threads", "4",
        "enable_keep_alive", "yes",
        NULL
    };

    struct mg_callbacks cb;
    memset(&cb, 0, sizeof(cb));
    cb.log_message = mg_log_message_cb;

    srv->mg = mg_start(&cb, srv, mg_opts);
    if (!srv->mg) {
        api_http_stop(srv);
        return STATUS_ERR_INVALID;
    }

    mg_set_request_handler(srv->mg, "/api/", api_request_handler, srv);

    *out = srv;
    return STATUS_OK;
}

void api_http_stop(ApiHttpServer *srv) {
    if (!srv) return;
    if (srv->mg) {
        mg_stop(srv->mg);
        srv->mg = NULL;
    }
    if (srv->mg_initialized) {
        mg_exit_library();
        srv->mg_initialized = 0;
    }
    free(srv);
}
