#include "api_json.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __has_include
#  if __has_include(<cjson/cJSON.h>)
#    include <cjson/cJSON.h>
#  else
#    include <cJSON.h>
#  endif
#else
#  include <cjson/cJSON.h>
#endif

static const char *bus_state_str(SimBusState st) {
    switch (st) {
        case SIM_BUS_RUNNING:  return "running";
        case SIM_BUS_DEADHEAD: return "deadhead";
        case SIM_BUS_RESERVE:  return "reserve";
        case SIM_BUS_BROKEN:   return "broken";
        default:               return "unknown";
    }
}

static const char *policy_str(SimPolicy p) {
    switch (p) {
        case SIM_POLICY_FIXED_EQUAL:        return "fixed_equal";
        case SIM_POLICY_FIXED_PROPORTIONAL: return "fixed_proportional";
        case SIM_POLICY_GREEDY_SPEC:        return "greedy_spec";
        case SIM_POLICY_GREEDY_MARGINAL:    return "greedy_marginal";
        default:                            return "fixed_equal";
    }
}

static const char *day_type_str(DayType d) {
    switch (d) {
        case DAY_WEEKDAY: return "weekday";
        case DAY_WEEKEND: return "weekend";
        case DAY_HOLIDAY: return "holiday";
        default:          return "weekday";
    }
}

static const char *stop_type_str(StopType t) {
    switch (t) {
        case STOP_HUB:         return "hub";
        case STOP_OFFICE:      return "office";
        case STOP_MARKET:      return "market";
        case STOP_RESIDENTIAL: return "residential";
        default:               return "unknown";
    }
}

static void format_hhmm(double min_val, char buf[16]) {
    int total = (int)floor(min_val + 1e-9);
    if (total < 0) total = 0;
    int hh = (total / 60) % 24;
    int mm = total % 60;
    snprintf(buf, 16, "%02d:%02d", hh, mm);
}

static double sanitize_double(double v) {
    return isfinite(v) ? v : 0.0;
}

static Status print_and_free(cJSON *root, char **out) {
    if (!root) return STATUS_ERR_NOMEM;
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!s) return STATUS_ERR_NOMEM;
    *out = s;
    return STATUS_OK;
}

Status api_parse_int_strict(const char *s, int min_val, int max_val, int *out) {
    if (!s || !out) return STATUS_ERR_NULL;
    if (s[0] == '\0' || min_val > max_val) return STATUS_ERR_INVALID;

    size_t i = 0;
    if (s[0] == '-') {
        if (!isdigit((unsigned char)s[1])) return STATUS_ERR_INVALID;
        i = 1;
    } else if (!isdigit((unsigned char)s[0])) {
        return STATUS_ERR_INVALID;
    }
    for (; s[i] != '\0'; i++) {
        if (!isdigit((unsigned char)s[i])) return STATUS_ERR_INVALID;
    }

    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (errno == ERANGE || !end || *end != '\0') return STATUS_ERR_INVALID;
    if (v < (long)min_val || v > (long)max_val) return STATUS_ERR_INVALID;

    *out = (int)v;
    return STATUS_OK;
}

Status api_parse_double_strict(const char *s, double min_val, double max_val, double *out) {
    if (!s || !out) return STATUS_ERR_NULL;
    if (s[0] == '\0' || isspace((unsigned char)s[0]) || s[0] == '+') return STATUS_ERR_INVALID;
    if (min_val > max_val) return STATUS_ERR_INVALID;

    errno = 0;
    char *end = NULL;
    double v = strtod(s, &end);
    if (errno == ERANGE || !end || *end != '\0' || !isfinite(v)) return STATUS_ERR_INVALID;
    if (v < min_val || v > max_val) return STATUS_ERR_INVALID;

    *out = v;
    return STATUS_OK;
}

int api_http_status_from_error(Status st, const char **code_out) {
    const char *code = "internal";
    int http = 500;
    switch (st) {
        case STATUS_ERR_INVALID:
            http = 400;
            code = "invalid_argument";
            break;
        case STATUS_ERR_NOT_FOUND:
            http = 404;
            code = "not_found";
            break;
        case STATUS_ERR_EMPTY:
        case STATUS_ERR_DUPLICATE:
            http = 409;
            code = "conflict";
            break;
        case STATUS_ERR_NULL:
        case STATUS_ERR_NOMEM:
        default:
            http = 500;
            code = "internal";
            break;
    }
    if (code_out) *code_out = code;
    return http;
}

char *api_json_error(const char *code, const char *message) {
    cJSON *root = cJSON_CreateObject();
    cJSON *err = cJSON_CreateObject();
    if (!root || !err) {
        cJSON_Delete(root);
        cJSON_Delete(err);
        return NULL;
    }
    cJSON_AddStringToObject(err, "code", code ? code : "internal");
    cJSON_AddStringToObject(err, "message", message ? message : "error");
    cJSON_AddItemToObject(root, "error", err);

    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

Status api_json_health(const AppCtx *ctx, char **out) {
    if (!ctx || !out) return STATUS_ERR_NULL;
    *out = NULL;
    cJSON *root = cJSON_CreateObject();
    if (!root) return STATUS_ERR_NOMEM;
    if (!cJSON_AddBoolToObject(root, "ok", 1) ||
        !cJSON_AddBoolToObject(root, "sim_running", app_is_running(ctx) ? 1 : 0)) {
        cJSON_Delete(root);
        return STATUS_ERR_NOMEM;
    }
    return print_and_free(root, out);
}

Status api_json_config(const AppCtx *ctx, char **out) {
    if (!ctx || !out) return STATUS_ERR_NULL;
    *out = NULL;
    const AppOptions *opt = app_options(ctx);
    const SimConfig *sc = app_sim_config(ctx);
    const NetworkModel *net = app_network(ctx);
    if (!opt || !sc || !net) return STATUS_ERR_NULL;

    cJSON *root = cJSON_CreateObject();
    cJSON *models = cJSON_CreateObject();
    if (!root || !models) {
        cJSON_Delete(root);
        cJSON_Delete(models);
        return STATUS_ERR_NOMEM;
    }

    cJSON_AddNumberToObject(root, "stops", (double)network_stop_count(net));
    cJSON_AddNumberToObject(root, "routes", (double)network_route_count(net));
    cJSON_AddNumberToObject(root, "buses", (double)opt->n_buses);
    cJSON_AddNumberToObject(root, "reserve", (double)opt->n_reserve);
    cJSON_AddNumberToObject(root, "seed", (double)opt->seed);
    cJSON_AddNumberToObject(root, "start_min", sc->start_min);
    cJSON_AddNumberToObject(root, "end_min", sc->end_min);
    cJSON_AddNumberToObject(root, "slice_min", sc->slice_min);
    cJSON_AddStringToObject(root, "policy", policy_str(sc->policy));
    cJSON_AddStringToObject(root, "predictor", app_active_predictor_name(ctx));
    cJSON_AddStringToObject(root, "spacing",
                            (sc->initial_spacing == SIM_SPACING_BY_STOP) ? "stop" : "time");
    cJSON_AddStringToObject(root, "reassign",
                            (sc->reassign_mode == SIM_REASSIGN_DUMP) ? "dump" : "drain");

    cJSON_AddBoolToObject(models, "ma", app_predictor_ma(ctx) != NULL);
    cJSON_AddBoolToObject(models, "rf", app_predictor_rf(ctx) != NULL);
    cJSON_AddItemToObject(root, "models", models);
    cJSON_AddBoolToObject(root, "offline", 1);

    return print_and_free(root, out);
}

Status api_json_graph(const AppCtx *ctx, char **out) {
    if (!ctx || !out) return STATUS_ERR_NULL;
    *out = NULL;
    const Graph *g = app_graph(ctx);
    if (!g) return STATUS_ERR_NULL;

    cJSON *root = cJSON_CreateObject();
    cJSON *nodes = cJSON_CreateArray();
    cJSON *edges = cJSON_CreateArray();
    if (!root || !nodes || !edges) {
        cJSON_Delete(root);
        cJSON_Delete(nodes);
        cJSON_Delete(edges);
        return STATUS_ERR_NOMEM;
    }

    size_t n_nodes = graph_node_count(g);
    for (size_t i = 0; i < n_nodes; i++) {
        const GraphNode *gn = graph_node(g, (int)i);
        cJSON *row = cJSON_CreateArray();
        if (!gn || !row) {
            cJSON_Delete(row);
            cJSON_Delete(nodes);
            cJSON_Delete(edges);
            cJSON_Delete(root);
            return STATUS_ERR_NOMEM;
        }
        cJSON_AddItemToArray(row, cJSON_CreateNumber(gn->lat));
        cJSON_AddItemToArray(row, cJSON_CreateNumber(gn->lng));
        cJSON_AddItemToArray(row, cJSON_CreateNumber((double)(gn->is_stop ? 1 : 0)));
        cJSON_AddItemToArray(nodes, row);
    }

    size_t n_edges = graph_edge_count(g);
    for (size_t i = 0; i < n_edges; i++) {
        const Edge *e = graph_edge(g, (int)i);
        const EdgeMeta *meta = graph_edge_meta(g, (int)i);
        cJSON *row = cJSON_CreateArray();
        if (!e || !row) {
            cJSON_Delete(row);
            cJSON_Delete(nodes);
            cJSON_Delete(edges);
            cJSON_Delete(root);
            return STATUS_ERR_NOMEM;
        }
        int rcls = meta ? (int)meta->road_class : (int)ROAD_OTHER;
        cJSON_AddItemToArray(row, cJSON_CreateNumber((double)e->from));
        cJSON_AddItemToArray(row, cJSON_CreateNumber((double)e->to));
        cJSON_AddItemToArray(row, cJSON_CreateNumber((double)rcls));
        cJSON_AddItemToArray(edges, row);
    }

    cJSON_AddItemToObject(root, "nodes", nodes);
    cJSON_AddItemToObject(root, "edges", edges);
    return print_and_free(root, out);
}

Status api_json_stops(const AppCtx *ctx, char **out) {
    if (!ctx || !out) return STATUS_ERR_NULL;
    *out = NULL;
    const NetworkModel *net = app_network(ctx);
    const Sim *sim = app_sim_const(ctx);
    size_t n_stops = 0;
    const DemandStop *stops = app_stops(ctx, &n_stops);
    if (!net || !sim || !stops) return STATUS_ERR_NULL;

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    if (!root || !arr) {
        cJSON_Delete(root);
        cJSON_Delete(arr);
        return STATUS_ERR_NOMEM;
    }

    for (size_t i = 0; i < n_stops; i++) {
        cJSON *obj = cJSON_CreateObject();
        if (!obj) {
            cJSON_Delete(arr);
            cJSON_Delete(root);
            return STATUS_ERR_NOMEM;
        }
        cJSON_AddNumberToObject(obj, "idx", (double)i);
        cJSON_AddNumberToObject(obj, "node", (double)stops[i].node);
        cJSON_AddStringToObject(obj, "name", stops[i].name);
        cJSON_AddStringToObject(obj, "type", stop_type_str(stops[i].type));
        cJSON_AddNumberToObject(obj, "scale", stops[i].scale);
        cJSON_AddNumberToObject(obj, "lat", stops[i].lat);
        cJSON_AddNumberToObject(obj, "lng", stops[i].lng);
        cJSON_AddNumberToObject(obj, "route", (double)network_stop_route(net, i));
        cJSON_AddNumberToObject(obj, "pos", (double)network_stop_pos(net, i));
        cJSON_AddBoolToObject(obj, "event_active", sim_event_active(sim, i) ? 1 : 0);
        cJSON_AddNumberToObject(obj, "first_flag_min", sanitize_double(sim_first_flag_min(sim, i)));
        cJSON_AddItemToArray(arr, obj);
    }

    cJSON_AddItemToObject(root, "stops", arr);
    return print_and_free(root, out);
}

Status api_json_routes(const AppCtx *ctx, char **out) {
    if (!ctx || !out) return STATUS_ERR_NULL;
    *out = NULL;
    const NetworkModel *net = app_network(ctx);
    const Sim *sim = app_sim_const(ctx);
    const Registry *reg = app_registry_const(ctx);
    const SimConfig *sc = app_sim_config(ctx);
    if (!net || !sim || !reg || !sc) return STATUS_ERR_NULL;

    size_t n_routes = network_route_count(net);
    size_t n_stops = network_stop_count(net);
    int hour = (int)floor(sim_now_min(sim) / 60.0);
    if (hour < 0) hour = 0;
    if (hour > 23) hour = 23;

    double *demands = (double *)calloc(n_routes > 0 ? n_routes : 1, sizeof(double));
    int *counts = (int *)calloc(n_routes > 0 ? n_routes : 1, sizeof(int));
    RouteBoardRow *board = (RouteBoardRow *)calloc(n_routes > 0 ? n_routes : 1, sizeof(RouteBoardRow));
    int *ev_stops = (int *)calloc(n_stops > 0 ? n_stops : 1, sizeof(int));
    if (!demands || !counts || !board || !ev_stops) {
        free(demands); free(counts); free(board); free(ev_stops);
        return STATUS_ERR_NOMEM;
    }

    size_t n_ev = 0;
    for (size_t s = 0; s < n_stops; s++) {
        if (sim_event_active(sim, s)) {
            ev_stops[n_ev++] = (int)s;
        }
    }

    const DemandPredictor *pred = app_active_predictor(ctx);
    Status st = network_route_demands(net, pred, hour, sc->day_type,
                                      sim_rain_active(sim), ev_stops, n_ev, demands);
    free(ev_stops);
    if (st != STATUS_OK) {
        free(demands); free(counts); free(board);
        return st;
    }

    for (size_t r = 0; r < n_routes; r++) {
        counts[r] = (int)sim_route_bus_count(sim, r);
    }

    st = registry_route_board(reg, demands, counts, board, n_routes);
    if (st != STATUS_OK) {
        free(demands); free(counts); free(board);
        return st;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *routes_arr = cJSON_CreateArray();
    cJSON *board_arr = cJSON_CreateArray();
    if (!root || !routes_arr || !board_arr) {
        cJSON_Delete(root); cJSON_Delete(routes_arr); cJSON_Delete(board_arr);
        free(demands); free(counts); free(board);
        return STATUS_ERR_NOMEM;
    }

    cJSON_AddNumberToObject(root, "hour", (double)hour);
    cJSON_AddStringToObject(root, "basis", pred ? "forecast" : "oracle");

    for (size_t r = 0; r < n_routes; r++) {
        cJSON *robj = cJSON_CreateObject();
        cJSON *sarr = cJSON_CreateArray();
        if (!robj || !sarr) {
            cJSON_Delete(robj); cJSON_Delete(sarr);
            cJSON_Delete(routes_arr); cJSON_Delete(board_arr); cJSON_Delete(root);
            free(demands); free(counts); free(board);
            return STATUS_ERR_NOMEM;
        }
        cJSON_AddNumberToObject(robj, "id", (double)r);
        const char *rname = network_route_name(net, r);
        cJSON_AddStringToObject(robj, "name", rname ? rname : "");

        size_t scnt = network_route_stop_count(net, r);
        for (size_t k = 0; k < scnt; k++) {
            cJSON_AddItemToArray(sarr, cJSON_CreateNumber((double)network_route_stop(net, r, k)));
        }
        cJSON_AddItemToObject(robj, "stops", sarr);

        double loop_s = network_route_loop_time_s(net, r, COST_TRAFFIC);
        if (loop_s < 0.0) {
            loop_s = network_route_loop_time_s(net, r, COST_STATIC);
        }
        double cycle_s = network_route_cycle_s(net, r);
        double prio = demands[r] / (double)(counts[r] + 1);

        cJSON_AddNumberToObject(robj, "loop_min", sanitize_double(loop_s / 60.0));
        cJSON_AddNumberToObject(robj, "cycle_min", sanitize_double(cycle_s / 60.0));
        cJSON_AddNumberToObject(robj, "buses", (double)counts[r]);
        cJSON_AddNumberToObject(robj, "demand", demands[r]);
        cJSON_AddNumberToObject(robj, "priority", prio);
        cJSON_AddItemToArray(routes_arr, robj);
    }

    for (size_t i = 0; i < n_routes; i++) {
        cJSON *bobj = cJSON_CreateObject();
        if (!bobj) {
            cJSON_Delete(routes_arr); cJSON_Delete(board_arr); cJSON_Delete(root);
            free(demands); free(counts); free(board);
            return STATUS_ERR_NOMEM;
        }
        cJSON_AddNumberToObject(bobj, "route_id", (double)board[i].route_id);
        cJSON_AddNumberToObject(bobj, "priority", board[i].priority);
        cJSON_AddNumberToObject(bobj, "demand", board[i].demand);
        cJSON_AddNumberToObject(bobj, "buses", (double)board[i].buses);
        cJSON_AddItemToArray(board_arr, bobj);
    }

    free(demands);
    free(counts);
    free(board);

    cJSON_AddItemToObject(root, "routes", routes_arr);
    cJSON_AddItemToObject(root, "board", board_arr);
    return print_and_free(root, out);
}

Status api_json_geometry(const AppCtx *ctx, char **out) {
    if (!ctx || !out) return STATUS_ERR_NULL;
    *out = NULL;
    const Graph *g = app_graph(ctx);
    const NetworkModel *net = app_network(ctx);
    if (!g || !net) return STATUS_ERR_NULL;

    cJSON *root = cJSON_CreateObject();
    cJSON *legs = cJSON_CreateArray();
    cJSON *blocked = cJSON_CreateArray();
    if (!root || !legs || !blocked) {
        cJSON_Delete(root); cJSON_Delete(legs); cJSON_Delete(blocked);
        return STATUS_ERR_NOMEM;
    }

    cJSON_AddNumberToObject(root, "version", (double)app_geometry_version(ctx));

    size_t n_routes = network_route_count(net);
    for (size_t r = 0; r < n_routes; r++) {
        size_t scnt = network_route_stop_count(net, r);
        for (size_t k = 0; k < scnt; k++) {
            int from_stop = network_route_stop(net, r, k);
            int to_stop = network_route_stop(net, r, (k + 1) % scnt);
            const PathResult *p = network_leg_path(net, r, k);
            double t_s = network_leg_time_s(net, r, k, COST_TRAFFIC);

            cJSON *lobj = cJSON_CreateObject();
            cJSON *parr = cJSON_CreateArray();
            if (!lobj || !parr) {
                cJSON_Delete(lobj); cJSON_Delete(parr);
                cJSON_Delete(legs); cJSON_Delete(blocked); cJSON_Delete(root);
                return STATUS_ERR_NOMEM;
            }
            cJSON_AddNumberToObject(lobj, "route", (double)r);
            cJSON_AddNumberToObject(lobj, "leg", (double)k);
            cJSON_AddNumberToObject(lobj, "from_stop", (double)from_stop);
            cJSON_AddNumberToObject(lobj, "to_stop", (double)to_stop);
            cJSON_AddNumberToObject(lobj, "time_s", sanitize_double(t_s));

            if (p && p->found && p->nodes) {
                for (size_t idx = 0; idx < p->node_count; idx++) {
                    const GraphNode *gn = graph_node(g, p->nodes[idx]);
                    if (!gn) continue;
                    cJSON *pt = cJSON_CreateArray();
                    if (!pt) {
                        cJSON_Delete(parr); cJSON_Delete(lobj);
                        cJSON_Delete(legs); cJSON_Delete(blocked); cJSON_Delete(root);
                        return STATUS_ERR_NOMEM;
                    }
                    cJSON_AddItemToArray(pt, cJSON_CreateNumber(gn->lat));
                    cJSON_AddItemToArray(pt, cJSON_CreateNumber(gn->lng));
                    cJSON_AddItemToArray(parr, pt);
                }
            }
            cJSON_AddItemToObject(lobj, "path", parr);
            cJSON_AddItemToArray(legs, lobj);
        }
    }

    size_t n_edges = graph_edge_count(g);
    for (size_t e_idx = 0; e_idx < n_edges; e_idx++) {
        const Edge *e = graph_edge(g, (int)e_idx);
        if (!e || !e->blocked) continue;
        const GraphNode *n1 = graph_node(g, e->from);
        const GraphNode *n2 = graph_node(g, e->to);
        if (!n1 || !n2) continue;

        cJSON *seg = cJSON_CreateArray();
        cJSON *p1 = cJSON_CreateArray();
        cJSON *p2 = cJSON_CreateArray();
        if (!seg || !p1 || !p2) {
            cJSON_Delete(seg); cJSON_Delete(p1); cJSON_Delete(p2);
            cJSON_Delete(legs); cJSON_Delete(blocked); cJSON_Delete(root);
            return STATUS_ERR_NOMEM;
        }
        cJSON_AddItemToArray(p1, cJSON_CreateNumber(n1->lat));
        cJSON_AddItemToArray(p1, cJSON_CreateNumber(n1->lng));
        cJSON_AddItemToArray(p2, cJSON_CreateNumber(n2->lat));
        cJSON_AddItemToArray(p2, cJSON_CreateNumber(n2->lng));
        cJSON_AddItemToArray(seg, p1);
        cJSON_AddItemToArray(seg, p2);
        cJSON_AddItemToArray(blocked, seg);
    }

    cJSON_AddItemToObject(root, "legs", legs);
    cJSON_AddItemToObject(root, "blocked", blocked);
    return print_and_free(root, out);
}

Status api_json_state(AppCtx *ctx, char **out) {
    if (!ctx || !out) return STATUS_ERR_NULL;
    *out = NULL;
    Sim *sim = app_sim(ctx);
    const NetworkModel *net = app_network(ctx);
    const SimConfig *sc = app_sim_config(ctx);
    if (!sim || !net || !sc) return STATUS_ERR_NULL;

    RouteManager *rm = network_route_manager(net);
    double now_min = sim_now_min(sim);
    char hhmm[16];
    format_hhmm(now_min, hhmm);

    SimStats st;
    memset(&st, 0, sizeof(st));
    Status sret = sim_stats(sim, &st);
    if (sret != STATUS_OK) return sret;

    cJSON *root = cJSON_CreateObject();
    cJSON *buses_arr = cJSON_CreateArray();
    cJSON *queues_arr = cJSON_CreateArray();
    cJSON *summary = cJSON_CreateObject();
    if (!root || !buses_arr || !queues_arr || !summary) {
        cJSON_Delete(root); cJSON_Delete(buses_arr); cJSON_Delete(queues_arr); cJSON_Delete(summary);
        return STATUS_ERR_NOMEM;
    }

    cJSON_AddNumberToObject(root, "clock_min", now_min);
    cJSON_AddStringToObject(root, "clock", hhmm);
    cJSON_AddBoolToObject(root, "running", app_is_running(ctx) ? 1 : 0);
    cJSON_AddBoolToObject(root, "finished", sim_finished(sim) ? 1 : 0);
    cJSON_AddNumberToObject(root, "speed", app_speed(ctx));
    cJSON_AddStringToObject(root, "day_type", day_type_str(sc->day_type));
    cJSON_AddBoolToObject(root, "rain", sim_rain_active(sim) ? 1 : 0);
    cJSON_AddStringToObject(root, "policy", policy_str(sim_policy(sim)));
    cJSON_AddStringToObject(root, "traffic_source", "simulated");
    cJSON_AddNumberToObject(root, "geometry_version", (double)app_geometry_version(ctx));
    cJSON_AddNumberToObject(root, "history_size", (double)(rm ? rm_history_size(rm) : 0U));

    size_t n_buses = sim_bus_count(sim);
    for (size_t i = 0; i < n_buses; i++) {
        SimBusInfo bi;
        SimBusMotion bm;
        memset(&bi, 0, sizeof(bi));
        memset(&bm, 0, sizeof(bm));
        sim_bus_info(sim, i, &bi);
        sim_bus_motion(sim, i, &bm);

        cJSON *bobj = cJSON_CreateObject();
        cJSON *mobj = cJSON_CreateObject();
        if (!bobj || !mobj) {
            cJSON_Delete(bobj); cJSON_Delete(mobj);
            cJSON_Delete(buses_arr); cJSON_Delete(queues_arr); cJSON_Delete(summary); cJSON_Delete(root);
            return STATUS_ERR_NOMEM;
        }
        cJSON_AddNumberToObject(bobj, "id", (double)bi.id);
        cJSON_AddStringToObject(bobj, "state", bus_state_str(bi.state));
        cJSON_AddNumberToObject(bobj, "route", (double)bi.route);
        cJSON_AddNumberToObject(bobj, "stop_idx", (double)bi.stop_idx);
        cJSON_AddNumberToObject(bobj, "load", (double)bi.load);
        cJSON_AddNumberToObject(bobj, "pending_route", (double)bi.pending_route);

        cJSON_AddBoolToObject(mobj, "moving", bm.moving ? 1 : 0);
        cJSON_AddNumberToObject(mobj, "leg_route", (double)bm.route);
        cJSON_AddNumberToObject(mobj, "leg_index", (double)bm.leg_index);
        cJSON_AddNumberToObject(mobj, "from_stop", (double)bm.from_stop);
        cJSON_AddNumberToObject(mobj, "to_stop", (double)bm.to_stop);
        cJSON_AddNumberToObject(mobj, "depart_s", bm.depart_s);
        cJSON_AddNumberToObject(mobj, "arrive_s", bm.arrive_s);
        cJSON_AddItemToObject(bobj, "motion", mobj);

        cJSON_AddItemToArray(buses_arr, bobj);
    }

    size_t n_stops = network_stop_count(net);
    for (size_t i = 0; i < n_stops; i++) {
        long q = sim_queue_length(sim, i);
        cJSON *qobj = cJSON_CreateObject();
        if (!qobj) {
            cJSON_Delete(buses_arr); cJSON_Delete(queues_arr); cJSON_Delete(summary); cJSON_Delete(root);
            return STATUS_ERR_NOMEM;
        }
        cJSON_AddNumberToObject(qobj, "stop", (double)i);
        cJSON_AddNumberToObject(qobj, "waiting", (double)(q >= 0 ? q : 0L));
        cJSON_AddItemToArray(queues_arr, qobj);
    }

    cJSON_AddNumberToObject(summary, "arrived", (double)st.arrived);
    cJSON_AddNumberToObject(summary, "boarded", (double)st.boarded);
    cJSON_AddNumberToObject(summary, "still_waiting", (double)st.still_waiting);
    cJSON_AddNumberToObject(summary, "mean_wait_boarded_s", sanitize_double(st.mean_wait_boarded_s));
    cJSON_AddNumberToObject(summary, "mean_wait_incl_censored_s", sanitize_double(st.mean_wait_incl_censored_s));
    cJSON_AddNumberToObject(summary, "p95_s", sanitize_double(st.p95_s));

    cJSON_AddItemToObject(root, "buses", buses_arr);
    cJSON_AddItemToObject(root, "queues", queues_arr);
    cJSON_AddItemToObject(root, "summary", summary);
    return print_and_free(root, out);
}

Status api_json_bus(const AppCtx *ctx, int bus_id, char **out) {
    if (!ctx || !out) return STATUS_ERR_NULL;
    *out = NULL;
    const Sim *sim = app_sim_const(ctx);
    const Registry *reg = app_registry_const(ctx);
    if (!sim || !reg) return STATUS_ERR_NULL;

    if (bus_id < 0 || (size_t)bus_id >= sim_bus_count(sim)) {
        return STATUS_ERR_NOT_FOUND;
    }
    const Bus *rb = registry_bus(reg, bus_id);
    if (!rb) return STATUS_ERR_NOT_FOUND;

    SimBusInfo bi;
    memset(&bi, 0, sizeof(bi));
    Status st = sim_bus_info(sim, (size_t)bus_id, &bi);
    if (st != STATUS_OK) return st;

    JourneyRecord *recs = (JourneyRecord *)calloc(REGISTRY_MAX_JOURNEYS, sizeof(JourneyRecord));
    if (!recs) return STATUS_ERR_NOMEM;
    size_t n_journeys = 0;
    st = registry_journeys(reg, bus_id, recs, REGISTRY_MAX_JOURNEYS, &n_journeys);
    if (st != STATUS_OK) {
        free(recs);
        return st;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *jarr = cJSON_CreateArray();
    if (!root || !jarr) {
        cJSON_Delete(root); cJSON_Delete(jarr);
        free(recs);
        return STATUS_ERR_NOMEM;
    }

    cJSON_AddNumberToObject(root, "id", (double)bi.id);
    cJSON_AddStringToObject(root, "state", bus_state_str(bi.state));
    cJSON_AddNumberToObject(root, "route", (double)bi.route);
    cJSON_AddNumberToObject(root, "stop_idx", (double)bi.stop_idx);
    cJSON_AddNumberToObject(root, "load", (double)bi.load);
    cJSON_AddNumberToObject(root, "capacity", (double)rb->capacity);
    cJSON_AddNumberToObject(root, "depot", (double)rb->depot_id);

    for (size_t i = 0; i < n_journeys; i++) {
        cJSON *jobj = cJSON_CreateObject();
        if (!jobj) {
            cJSON_Delete(jarr); cJSON_Delete(root);
            free(recs);
            return STATUS_ERR_NOMEM;
        }
        cJSON_AddNumberToObject(jobj, "trip_id", (double)recs[i].trip_id);
        cJSON_AddNumberToObject(jobj, "route_id", (double)recs[i].route_id);
        cJSON_AddNumberToObject(jobj, "start_min", recs[i].start_time);
        cJSON_AddNumberToObject(jobj, "end_min", recs[i].end_time);
        cJSON_AddNumberToObject(jobj, "passengers", (double)recs[i].passengers_carried);
        cJSON_AddItemToArray(jarr, jobj);
    }

    free(recs);
    cJSON_AddItemToObject(root, "journeys", jarr);
    return print_and_free(root, out);
}

Status api_json_schedule(const AppCtx *ctx, int has_from, int from_min,
                         int has_to, int to_min, int has_route, int route_id, char **out) {
    if (!ctx || !out) return STATUS_ERR_NULL;
    *out = NULL;
    const Sim *sim = app_sim_const(ctx);
    const NetworkModel *net = app_network(ctx);
    const Registry *reg = app_registry_const(ctx);
    if (!sim || !net || !reg) return STATUS_ERR_NULL;

    int eff_from = from_min;
    int eff_to = to_min;
    int eff_route = has_route ? route_id : -1;

    if (!has_from) {
        eff_from = (int)floor(sim_now_min(sim));
        if (eff_from < 0) eff_from = 0;
        if (eff_from >= 1440) eff_from = 1320;
    }
    if (!has_to) {
        eff_to = eff_from + 120;
        if (eff_to > 1440) eff_to = 1440;
    }

    if (eff_from < 0 || eff_to > 1440 || eff_from >= eff_to) {
        return STATUS_ERR_INVALID;
    }
    if (eff_route < -1 || eff_route >= (int)network_route_count(net)) {
        return STATUS_ERR_INVALID;
    }

    Trip *trips = (Trip *)calloc(API_MAX_SCHEDULE_TRIPS, sizeof(Trip));
    if (!trips) return STATUS_ERR_NOMEM;

    size_t n_trips = 0;
    Status st = registry_trips(reg, eff_from, eff_to, eff_route, trips, API_MAX_SCHEDULE_TRIPS, &n_trips);
    if (st != STATUS_OK) {
        free(trips);
        return st;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *avl = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    if (!root || !avl || !arr) {
        cJSON_Delete(root); cJSON_Delete(avl); cJSON_Delete(arr);
        free(trips);
        return STATUS_ERR_NOMEM;
    }

    cJSON_AddNumberToObject(root, "from", (double)eff_from);
    cJSON_AddNumberToObject(root, "to", (double)eff_to);
    cJSON_AddNumberToObject(root, "count", (double)n_trips);

    cJSON_AddNumberToObject(avl, "size", (double)registry_trip_count(reg));
    cJSON_AddNumberToObject(avl, "height", (double)registry_timetable_height(reg));
    cJSON_AddBoolToObject(avl, "balanced", registry_timetable_balanced(reg) == 1);
    cJSON_AddItemToObject(root, "avl", avl);

    for (size_t i = 0; i < n_trips; i++) {
        cJSON *tobj = cJSON_CreateObject();
        if (!tobj) {
            cJSON_Delete(arr); cJSON_Delete(root);
            free(trips);
            return STATUS_ERR_NOMEM;
        }
        char hhmm[16];
        format_hhmm((double)trips[i].departure_time, hhmm);
        cJSON_AddNumberToObject(tobj, "trip_id", (double)trips[i].trip_id);
        cJSON_AddNumberToObject(tobj, "route_id", (double)trips[i].route_id);
        cJSON_AddNumberToObject(tobj, "bus_id", (double)trips[i].bus_id);
        cJSON_AddNumberToObject(tobj, "departure_min", (double)trips[i].departure_time);
        cJSON_AddStringToObject(tobj, "departure", hhmm);
        cJSON_AddItemToArray(arr, tobj);
    }

    free(trips);
    cJSON_AddItemToObject(root, "trips", arr);
    return print_and_free(root, out);
}

Status api_json_tree(const AppCtx *ctx, char **out) {
    if (!ctx || !out) return STATUS_ERR_NULL;
    *out = NULL;
    const Registry *reg = app_registry_const(ctx);
    if (!reg) return STATUS_ERR_NULL;

    size_t tcnt = registry_tree_count(reg);
    RegTreeRow *rows = (RegTreeRow *)calloc(tcnt > 0 ? tcnt : 1, sizeof(RegTreeRow));
    if (!rows) return STATUS_ERR_NOMEM;

    size_t n_rows = 0;
    Status st = registry_tree_rows(reg, rows, tcnt, &n_rows);
    if (st != STATUS_OK) {
        free(rows);
        return st;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *nodes = cJSON_CreateArray();
    if (!root || !nodes) {
        cJSON_Delete(root); cJSON_Delete(nodes);
        free(rows);
        return STATUS_ERR_NOMEM;
    }

    cJSON_AddNumberToObject(root, "count", (double)n_rows);
    cJSON_AddNumberToObject(root, "height", (double)registry_tree_height(reg));

    for (size_t i = 0; i < n_rows; i++) {
        cJSON *obj = cJSON_CreateObject();
        if (!obj) {
            cJSON_Delete(nodes); cJSON_Delete(root);
            free(rows);
            return STATUS_ERR_NOMEM;
        }
        cJSON_AddNumberToObject(obj, "row", (double)i);
        cJSON_AddNumberToObject(obj, "parent", (double)rows[i].parent_row);
        cJSON_AddNumberToObject(obj, "depth", (double)rows[i].depth);
        cJSON_AddNumberToObject(obj, "kind", (double)rows[i].kind);
        cJSON_AddNumberToObject(obj, "id", (double)rows[i].id);
        cJSON_AddStringToObject(obj, "name", rows[i].name);
        cJSON_AddItemToArray(nodes, obj);
    }

    free(rows);
    cJSON_AddItemToObject(root, "nodes", nodes);
    return print_and_free(root, out);
}

Status api_json_forecast(const AppCtx *ctx, int stop_idx, char **out) {
    if (!ctx || !out) return STATUS_ERR_NULL;
    *out = NULL;
    const Sim *sim = app_sim_const(ctx);
    const SimConfig *sc = app_sim_config(ctx);
    size_t n_stops = 0;
    const DemandStop *stops = app_stops(ctx, &n_stops);
    if (!sim || !sc || !stops) return STATUS_ERR_NULL;

    if (stop_idx < 0 || (size_t)stop_idx >= n_stops) {
        return STATUS_ERR_NOT_FOUND;
    }

    const DemandStop *ds = &stops[stop_idx];
    int rain = sim_rain_active(sim);
    int event = sim_event_active(sim, (size_t)stop_idx);
    const DemandPredictor *ma = app_predictor_ma(ctx);
    const DemandPredictor *rf = app_predictor_rf(ctx);

    cJSON *root = cJSON_CreateObject();
    cJSON *hours = cJSON_CreateArray();
    if (!root || !hours) {
        cJSON_Delete(root); cJSON_Delete(hours);
        return STATUS_ERR_NOMEM;
    }

    cJSON_AddNumberToObject(root, "stop", (double)stop_idx);
    cJSON_AddStringToObject(root, "name", ds->name);
    cJSON_AddStringToObject(root, "type", stop_type_str(ds->type));
    cJSON_AddStringToObject(root, "day_type", day_type_str(sc->day_type));
    cJSON_AddBoolToObject(root, "rain", rain ? 1 : 0);
    cJSON_AddBoolToObject(root, "event_active", event ? 1 : 0);

    for (int h = 0; h < 24; h++) {
        cJSON *hobj = cJSON_CreateObject();
        if (!hobj) {
            cJSON_Delete(hours); cJSON_Delete(root);
            return STATUS_ERR_NOMEM;
        }
        double oracle = demand_expected(ds->type, ds->scale, h, sc->day_type, rain, event);
        cJSON_AddNumberToObject(hobj, "hour", (double)h);
        cJSON_AddNumberToObject(hobj, "oracle", sanitize_double(oracle));

        if (ma) {
            double v = dp_predict(ma, ds->node, ds->type, ds->scale, h, sc->day_type, rain, event);
            cJSON_AddNumberToObject(hobj, "ma", sanitize_double(v));
        } else {
            cJSON_AddNullToObject(hobj, "ma");
        }

        if (rf) {
            double v = dp_predict(rf, ds->node, ds->type, ds->scale, h, sc->day_type, rain, event);
            cJSON_AddNumberToObject(hobj, "rf", sanitize_double(v));
        } else {
            cJSON_AddNullToObject(hobj, "rf");
        }

        cJSON_AddNumberToObject(hobj, "observed", (double)sim_stop_hour_arrivals(sim, (size_t)stop_idx, h));
        cJSON_AddItemToArray(hours, hobj);
    }

    cJSON_AddItemToObject(root, "hours", hours);
    return print_and_free(root, out);
}

Status api_json_metrics(AppCtx *ctx, char **out) {
    if (!ctx || !out) return STATUS_ERR_NULL;
    *out = NULL;
    Sim *sim = app_sim(ctx);
    const NetworkModel *net = app_network(ctx);
    if (!sim || !net) return STATUS_ERR_NULL;

    SimStats st;
    memset(&st, 0, sizeof(st));
    Status sret = sim_stats(sim, &st);
    if (sret != STATUS_OK) return sret;

    cJSON *root = cJSON_CreateObject();
    cJSON *sim_obj = cJSON_CreateObject();
    cJSON *routes_arr = cJSON_CreateArray();
    cJSON *traffic_obj = cJSON_CreateObject();
    if (!root || !sim_obj || !routes_arr || !traffic_obj) {
        cJSON_Delete(root); cJSON_Delete(sim_obj); cJSON_Delete(routes_arr); cJSON_Delete(traffic_obj);
        return STATUS_ERR_NOMEM;
    }

    cJSON_AddNumberToObject(sim_obj, "arrived", (double)st.arrived);
    cJSON_AddNumberToObject(sim_obj, "boarded", (double)st.boarded);
    cJSON_AddNumberToObject(sim_obj, "still_waiting", (double)st.still_waiting);
    cJSON_AddNumberToObject(sim_obj, "wait_sum_s", sanitize_double(st.wait_sum_s));
    cJSON_AddNumberToObject(sim_obj, "wait_max_s", sanitize_double(st.wait_max_s));
    cJSON_AddNumberToObject(sim_obj, "mean_wait_boarded_s", sanitize_double(st.mean_wait_boarded_s));
    cJSON_AddNumberToObject(sim_obj, "mean_wait_incl_censored_s", sanitize_double(st.mean_wait_incl_censored_s));
    cJSON_AddNumberToObject(sim_obj, "p50_s", sanitize_double(st.p50_s));
    cJSON_AddNumberToObject(sim_obj, "p95_s", sanitize_double(st.p95_s));
    cJSON_AddNumberToObject(sim_obj, "full_bus_events", (double)st.full_bus_events);
    cJSON_AddNumberToObject(sim_obj, "forced_dumps", (double)st.forced_dumps);
    cJSON_AddNumberToObject(sim_obj, "stranded_by_breakdown", (double)st.stranded_by_breakdown);
    cJSON_AddNumberToObject(sim_obj, "bus_arrivals", (double)st.bus_arrivals);
    cJSON_AddNumberToObject(sim_obj, "moves_applied", (double)st.moves_applied);
    cJSON_AddNumberToObject(sim_obj, "deadhead_s_total", sanitize_double(st.deadhead_s_total));
    cJSON_AddNumberToObject(sim_obj, "anomaly_flags", (double)st.anomaly_flags);
    cJSON_AddNumberToObject(sim_obj, "emergency_dispatches", (double)st.emergency_dispatches);
    cJSON_AddNumberToObject(sim_obj, "stalled_s", sanitize_double(st.stalled_s));
    cJSON_AddNumberToObject(sim_obj, "mean_load_factor", sanitize_double(st.mean_load_factor));
    cJSON_AddNumberToObject(sim_obj, "decisions", (double)st.decisions);
    cJSON_AddNumberToObject(sim_obj, "decisions_applied", (double)st.decisions_applied);

    size_t n_routes = network_route_count(net);
    for (size_t r = 0; r < n_routes; r++) {
        SimRouteStats rs;
        memset(&rs, 0, sizeof(rs));
        sim_route_stats(sim, r, &rs);
        cJSON *robj = cJSON_CreateObject();
        if (!robj) {
            cJSON_Delete(sim_obj); cJSON_Delete(routes_arr); cJSON_Delete(traffic_obj); cJSON_Delete(root);
            return STATUS_ERR_NOMEM;
        }
        cJSON_AddNumberToObject(robj, "route", (double)r);
        cJSON_AddNumberToObject(robj, "boarded", (double)rs.boarded);
        cJSON_AddNumberToObject(robj, "still_waiting", (double)rs.still_waiting);
        cJSON_AddNumberToObject(robj, "mean_wait_s", sanitize_double(rs.mean_wait_s));
        cJSON_AddNumberToObject(robj, "max_wait_s", sanitize_double(rs.wait_max_s));
        cJSON_AddItemToArray(routes_arr, robj);
    }

    const char *ml_raw = app_ml_metrics_json(ctx);
    cJSON *ml_item = NULL;
    if (ml_raw) {
        ml_item = cJSON_Parse(ml_raw);
    }
    if (!ml_item) {
        ml_item = cJSON_CreateNull();
    }
    if (!ml_item) {
        cJSON_Delete(sim_obj); cJSON_Delete(routes_arr); cJSON_Delete(traffic_obj); cJSON_Delete(root);
        return STATUS_ERR_NOMEM;
    }

    cJSON_AddStringToObject(traffic_obj, "source", "simulated");
    cJSON_AddNumberToObject(traffic_obj, "calls", 0.0);
    cJSON_AddNumberToObject(traffic_obj, "live_corridors", 0.0);
    cJSON_AddStringToObject(traffic_obj, "last_error", "");

    cJSON_AddItemToObject(root, "sim", sim_obj);
    cJSON_AddItemToObject(root, "routes", routes_arr);
    cJSON_AddItemToObject(root, "ml", ml_item);
    cJSON_AddNullToObject(root, "last_optimize");
    cJSON_AddItemToObject(root, "traffic", traffic_obj);

    return print_and_free(root, out);
}

Status api_json_alerts(const AppCtx *ctx, char **out) {
    if (!ctx || !out) return STATUS_ERR_NULL;
    *out = NULL;
    const Sim *sim = app_sim_const(ctx);
    size_t n_stops = 0;
    const DemandStop *stops = app_stops(ctx, &n_stops);
    if (!sim || !stops) return STATUS_ERR_NULL;

    cJSON *root = cJSON_CreateObject();
    cJSON *flags_arr = cJSON_CreateArray();
    cJSON *log_arr = cJSON_CreateArray();
    if (!root || !flags_arr || !log_arr) {
        cJSON_Delete(root); cJSON_Delete(flags_arr); cJSON_Delete(log_arr);
        return STATUS_ERR_NOMEM;
    }

    size_t n_flags = sim_flag_count(sim);
    for (size_t i = 0; i < n_flags; i++) {
        AnomalyFlag fl;
        if (sim_flag(sim, i, &fl) != STATUS_OK) continue;
        cJSON *fobj = cJSON_CreateObject();
        if (!fobj) {
            cJSON_Delete(flags_arr); cJSON_Delete(log_arr); cJSON_Delete(root);
            return STATUS_ERR_NOMEM;
        }
        const char *sname = (fl.stop < n_stops) ? stops[fl.stop].name : "";
        cJSON_AddNumberToObject(fobj, "stop", (double)fl.stop);
        cJSON_AddStringToObject(fobj, "name", sname);
        cJSON_AddNumberToObject(fobj, "time_min", sanitize_double(sim_first_flag_min(sim, fl.stop)));
        cJSON_AddNumberToObject(fobj, "observed", (double)fl.observed);
        cJSON_AddNumberToObject(fobj, "expected", sanitize_double(fl.expected));
        cJSON_AddNumberToObject(fobj, "z", sanitize_double(fl.z_score));
        cJSON_AddItemToArray(flags_arr, fobj);
    }

    /* sim_log_count reports total logged lines; sim_log_line returns NULL past stored ring cap */
    size_t total_reported = sim_log_count(sim);
    size_t stored = 0;
    while (stored < total_reported && sim_log_line(sim, stored) != NULL) {
        stored++;
    }
    size_t start_idx = (stored > API_MAX_LOG_LINES) ? (stored - API_MAX_LOG_LINES) : 0U;
    for (size_t i = start_idx; i < stored; i++) {
        const char *line = sim_log_line(sim, i);
        if (line) {
            cJSON_AddItemToArray(log_arr, cJSON_CreateString(line));
        }
    }

    cJSON_AddItemToObject(root, "flags", flags_arr);
    cJSON_AddItemToObject(root, "log", log_arr);
    return print_and_free(root, out);
}

Status api_json_history(const AppCtx *ctx, char **out) {
    if (!ctx || !out) return STATUS_ERR_NULL;
    *out = NULL;
    const NetworkModel *net = app_network(ctx);
    if (!net) return STATUS_ERR_NULL;
    const RouteManager *rm = network_route_manager(net);

    cJSON *root = cJSON_CreateObject();
    cJSON *entries = cJSON_CreateArray();
    if (!root || !entries) {
        cJSON_Delete(root); cJSON_Delete(entries);
        return STATUS_ERR_NOMEM;
    }

    size_t hsz = rm ? rm_history_size(rm) : 0U;
    cJSON_AddNumberToObject(root, "size", (double)hsz);

    for (size_t i = 0; i < hsz; i++) {
        char buf[128];
        buf[0] = '\0';
        if (rm_history_describe(rm, i, buf, sizeof(buf)) == STATUS_OK) {
            cJSON_AddItemToArray(entries, cJSON_CreateString(buf));
        }
    }

    cJSON_AddItemToObject(root, "entries", entries);
    return print_and_free(root, out);
}

Status api_json_sim_control_parse(const char *body, char *action_buf, size_t action_cap,
                                  int *has_action, int *has_speed, double *speed_out) {
    if (!body || !action_buf || !has_action || !has_speed || !speed_out) {
        return STATUS_ERR_NULL;
    }
    if (action_cap == 0) return STATUS_ERR_INVALID;

    action_buf[0] = '\0';
    *has_action = 0;
    *has_speed = 0;
    *speed_out = 0.0;

    cJSON *root = cJSON_Parse(body);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return STATUS_ERR_INVALID;
    }

    cJSON *j_act = cJSON_GetObjectItemCaseSensitive(root, "action");
    if (j_act != NULL) {
        if (!cJSON_IsString(j_act) || !j_act->valuestring ||
            strlen(j_act->valuestring) >= action_cap) {
            cJSON_Delete(root);
            return STATUS_ERR_INVALID;
        }
        const char *a = j_act->valuestring;
        if (strcmp(a, "start") != 0 && strcmp(a, "pause") != 0 &&
            strcmp(a, "resume") != 0 && strcmp(a, "speed") != 0) {
            cJSON_Delete(root);
            return STATUS_ERR_INVALID;
        }
        snprintf(action_buf, action_cap, "%s", a);
        *has_action = 1;
    }

    cJSON *j_spd = cJSON_GetObjectItemCaseSensitive(root, "speed");
    if (j_spd != NULL) {
        if (!cJSON_IsNumber(j_spd) || !isfinite(j_spd->valuedouble) ||
            j_spd->valuedouble < 1.0 || j_spd->valuedouble > 3600.0) {
            cJSON_Delete(root);
            return STATUS_ERR_INVALID;
        }
        *has_speed = 1;
        *speed_out = j_spd->valuedouble;
    }

    cJSON_Delete(root);

    if (!*has_action && !*has_speed) return STATUS_ERR_INVALID;
    if (*has_action && strcmp(action_buf, "speed") == 0 && !*has_speed) {
        return STATUS_ERR_INVALID;
    }
    return STATUS_OK;
}

Status api_json_sim_control_ok(const AppCtx *ctx, char **out) {
    if (!ctx || !out) return STATUS_ERR_NULL;
    *out = NULL;
    const Sim *sim = app_sim_const(ctx);
    if (!sim) return STATUS_ERR_NULL;

    cJSON *root = cJSON_CreateObject();
    if (!root) return STATUS_ERR_NOMEM;
    cJSON_AddBoolToObject(root, "ok", 1);
    cJSON_AddBoolToObject(root, "running", app_is_running(ctx) ? 1 : 0);
    cJSON_AddNumberToObject(root, "speed", app_speed(ctx));
    cJSON_AddNumberToObject(root, "clock_min", sim_now_min(sim));
    return print_and_free(root, out);
}

