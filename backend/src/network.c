#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct NetworkRoute {
    int *tour;
    size_t stop_count;
    char name[64];
} NetworkRoute;

struct NetworkModel {
    Graph *g;
    RouteManager *rm;
    DemandStop *stops;
    size_t n_stops;
    size_t n_routes;
    double dwell_s_per_stop;
    int build_matrix;
    NetworkRoute *routes;
    int *stop_route;
    int *stop_pos;
    double *matrix;
};

void network_config_default(NetworkConfig *c) {
    if (!c) return;
    c->n_routes = 6;
    c->dwell_s_per_stop = NETWORK_DEFAULT_DWELL_S;
    c->build_matrix = 1;
}

typedef struct StopSortItem {
    size_t index;
    double lng;
    double lat;
} StopSortItem;

static int cmp_stop_strips(const void *a, const void *b) {
    const StopSortItem *sa = (const StopSortItem *)a;
    const StopSortItem *sb = (const StopSortItem *)b;
    if (sa->lng < sb->lng) return -1;
    if (sa->lng > sb->lng) return 1;
    if (sa->lat < sb->lat) return -1;
    if (sa->lat > sb->lat) return 1;
    if (sa->index < sb->index) return -1;
    if (sa->index > sb->index) return 1;
    return 0;
}

Status network_build(Graph *g, const DemandStop *stops, size_t n_stops, const NetworkConfig *cfg, NetworkModel **out) {
    if (!g || !stops || !out) {
        return STATUS_ERR_NULL;
    }
    *out = NULL;

    NetworkConfig default_cfg;
    if (!cfg) {
        network_config_default(&default_cfg);
        cfg = &default_cfg;
    }

    if (n_stops < 2) {
        return STATUS_ERR_INVALID;
    }
    if (cfg->n_routes < 1 || cfg->n_routes > n_stops / 2) {
        return STATUS_ERR_INVALID;
    }
    if (cfg->dwell_s_per_stop < 0.0) {
        return STATUS_ERR_INVALID;
    }

    size_t num_graph_nodes = graph_node_count(g);
    for (size_t i = 0; i < n_stops; i++) {
        int node_i = stops[i].node;
        if (node_i < 0 || (size_t)node_i >= num_graph_nodes) {
            return STATUS_ERR_INVALID;
        }
        for (size_t j = i + 1; j < n_stops; j++) {
            if (node_i == stops[j].node) {
                return STATUS_ERR_INVALID;
            }
        }
    }

    /* 1. graph_reset_traffic(g) */
    graph_reset_traffic(g);

    NetworkModel *net = (NetworkModel *)calloc(1, sizeof(NetworkModel));
    if (!net) return STATUS_ERR_NOMEM;

    net->g = g;
    net->n_stops = n_stops;
    net->n_routes = cfg->n_routes;
    net->dwell_s_per_stop = cfg->dwell_s_per_stop;
    net->build_matrix = cfg->build_matrix;

    net->stops = (DemandStop *)malloc(n_stops * sizeof(DemandStop));
    net->stop_route = (int *)malloc(n_stops * sizeof(int));
    net->stop_pos = (int *)malloc(n_stops * sizeof(int));
    net->routes = (NetworkRoute *)calloc(net->n_routes, sizeof(NetworkRoute));

    if (!net->stops || !net->stop_route || !net->stop_pos || !net->routes) {
        network_destroy(net);
        return STATUS_ERR_NOMEM;
    }
    memcpy(net->stops, stops, n_stops * sizeof(DemandStop));

    for (size_t i = 0; i < n_stops; i++) {
        net->stop_route[i] = -1;
        net->stop_pos[i] = -1;
    }

    /* 2. Partition into west-to-east strips: sort stop indices by (lng asc, lat asc, index asc) */
    StopSortItem *sort_items = (StopSortItem *)malloc(n_stops * sizeof(StopSortItem));
    if (!sort_items) {
        network_destroy(net);
        return STATUS_ERR_NOMEM;
    }
    for (size_t i = 0; i < n_stops; i++) {
        sort_items[i].index = i;
        sort_items[i].lng = stops[i].lng;
        sort_items[i].lat = stops[i].lat;
    }
    qsort(sort_items, n_stops, sizeof(StopSortItem), cmp_stop_strips);

    size_t R = net->n_routes;
    size_t base_size = n_stops / R;
    size_t rem = n_stops % R;

    net->rm = rm_create(g, 0.0);
    if (!net->rm) {
        free(sort_items);
        network_destroy(net);
        return STATUS_ERR_NOMEM;
    }

    size_t current_offset = 0;
    for (size_t r = 0; r < R; r++) {
        size_t group_size = base_size + (r < rem ? 1 : 0);
        if (group_size > 999) {
            free(sort_items);
            network_destroy(net);
            return STATUS_ERR_INVALID;
        }

        net->routes[r].stop_count = group_size;
        net->routes[r].tour = (int *)malloc(group_size * sizeof(int));
        if (!net->routes[r].tour) {
            free(sort_items);
            network_destroy(net);
            return STATUS_ERR_NOMEM;
        }

        /* Group stops are in sort_items[current_offset .. current_offset + group_size - 1] */
        int *group_stops = (int *)malloc(group_size * sizeof(int));
        char *visited = (char *)calloc(group_size, sizeof(char));
        if (!group_stops || !visited) {
            free(group_stops);
            free(visited);
            free(sort_items);
            network_destroy(net);
            return STATUS_ERR_NOMEM;
        }

        for (size_t k = 0; k < group_size; k++) {
            group_stops[k] = (int)sort_items[current_offset + k].index;
        }

        /* 3. Nearest-neighbour tour: start at stop with smallest (lat, index) */
        size_t best_start_idx = 0;
        double min_start_lat = stops[group_stops[0]].lat;
        int min_start_stop = group_stops[0];

        for (size_t k = 1; k < group_size; k++) {
            int s = group_stops[k];
            double s_lat = stops[s].lat;
            if (s_lat < min_start_lat || (s_lat == min_start_lat && s < min_start_stop)) {
                min_start_lat = s_lat;
                min_start_stop = s;
                best_start_idx = k;
            }
        }

        net->routes[r].tour[0] = group_stops[best_start_idx];
        visited[best_start_idx] = 1;
        int curr_stop = group_stops[best_start_idx];

        for (size_t step = 1; step < group_size; step++) {
            size_t best_next_idx = 0;
            double min_dist = 1e30;
            int best_stop_idx = 999999;
            int found = 0;

            for (size_t k = 0; k < group_size; k++) {
                if (visited[k]) continue;
                int candidate = group_stops[k];
                double d = graph_haversine_m(stops[curr_stop].lat, stops[curr_stop].lng,
                                            stops[candidate].lat, stops[candidate].lng);
                if (!found || d < min_dist || (d == min_dist && candidate < best_stop_idx)) {
                    min_dist = d;
                    best_stop_idx = candidate;
                    best_next_idx = k;
                    found = 1;
                }
            }

            net->routes[r].tour[step] = group_stops[best_next_idx];
            visited[best_next_idx] = 1;
            curr_stop = group_stops[best_next_idx];
        }

        free(group_stops);
        free(visited);

        /* Map stops to route and position */
        for (size_t k = 0; k < group_size; k++) {
            int s_idx = net->routes[r].tour[k];
            net->stop_route[s_idx] = (int)r;
            net->stop_pos[s_idx] = (int)k;
        }

        /* 4. Register each leg with rm_track_route(rm, r * 1000 + k, src, dst) */
        for (size_t k = 0; k < group_size; k++) {
            int src_idx = net->routes[r].tour[k];
            int dst_idx = net->routes[r].tour[(k + 1) % group_size];
            int src_node = stops[src_idx].node;
            int dst_node = stops[dst_idx].node;
            int leg_id = (int)(r * 1000 + k);

            Status st = rm_track_route(net->rm, leg_id, src_node, dst_node);
            if (st != STATUS_OK) {
                free(sort_items);
                network_destroy(net);
                return STATUS_ERR_INVALID;
            }
            const PathResult *path = rm_get_path(net->rm, leg_id);
            if (!path || path_time_s(g, path, COST_STATIC) < 0.0) {
                free(sort_items);
                network_destroy(net);
                return STATUS_ERR_INVALID;
            }
        }

        /* 5. Route name */
        char first_buf[64], last_buf[64];
        int first_s = net->routes[r].tour[0];
        int last_s = net->routes[r].tour[group_size - 1];

        if (stops[first_s].name[0] != '\0') {
            snprintf(first_buf, sizeof(first_buf), "%s", stops[first_s].name);
        } else {
            snprintf(first_buf, sizeof(first_buf), "stop %d", stops[first_s].node);
        }

        if (stops[last_s].name[0] != '\0') {
            snprintf(last_buf, sizeof(last_buf), "%s", stops[last_s].name);
        } else {
            snprintf(last_buf, sizeof(last_buf), "stop %d", stops[last_s].node);
        }

        char tmp_name[160];
        snprintf(tmp_name, sizeof(tmp_name), "R%zu: %s - %s", r + 1, first_buf, last_buf);
        strncpy(net->routes[r].name, tmp_name, sizeof(net->routes[r].name) - 1);
        net->routes[r].name[sizeof(net->routes[r].name) - 1] = '\0';
        current_offset += group_size;
    }
    free(sort_items);

    /* 6. Travel-time matrix between ALL ordered pairs (a != b) */
    if (cfg->build_matrix) {
        net->matrix = (double *)malloc(n_stops * n_stops * sizeof(double));
        if (!net->matrix) {
            network_destroy(net);
            return STATUS_ERR_NOMEM;
        }

        for (size_t a = 0; a < n_stops; a++) {
            for (size_t b = 0; b < n_stops; b++) {
                if (a == b) {
                    net->matrix[a * n_stops + b] = 0.0;
                } else {
                    PathResult res;
                    path_result_init(&res);
                    Status st = find_path(g, stops[a].node, stops[b].node, SEARCH_ASTAR, COST_STATIC, &res);
                    if (st != STATUS_OK || !res.found) {
                        path_result_free(&res);
                        network_destroy(net);
                        return STATUS_ERR_INVALID;
                    }
                    net->matrix[a * n_stops + b] = res.total_time_s;
                    path_result_free(&res);
                }
            }
        }
    }

    *out = net;
    return STATUS_OK;
}

void network_destroy(NetworkModel *n) {
    if (!n) return;
    if (n->routes) {
        for (size_t r = 0; r < n->n_routes; r++) {
            free(n->routes[r].tour);
        }
        free(n->routes);
    }
    free(n->stops);
    free(n->stop_route);
    free(n->stop_pos);
    free(n->matrix);
    if (n->rm) {
        rm_destroy(n->rm);
    }
    free(n);
}

Graph *network_graph(const NetworkModel *n) {
    return n ? n->g : NULL;
}

RouteManager *network_route_manager(const NetworkModel *n) {
    return n ? n->rm : NULL;
}

size_t network_route_count(const NetworkModel *n) {
    return n ? n->n_routes : 0;
}

size_t network_stop_count(const NetworkModel *n) {
    return n ? n->n_stops : 0;
}

const DemandStop *network_stop(const NetworkModel *n, size_t stop_idx) {
    if (!n || stop_idx >= n->n_stops) return NULL;
    return &n->stops[stop_idx];
}

size_t network_route_stop_count(const NetworkModel *n, size_t r) {
    if (!n || r >= n->n_routes) return 0;
    return n->routes[r].stop_count;
}

int network_route_stop(const NetworkModel *n, size_t r, size_t k) {
    if (!n || r >= n->n_routes || k >= n->routes[r].stop_count) return -1;
    return n->routes[r].tour[k];
}

int network_stop_route(const NetworkModel *n, size_t stop_idx) {
    if (!n || stop_idx >= n->n_stops) return -1;
    return n->stop_route[stop_idx];
}

int network_stop_pos(const NetworkModel *n, size_t stop_idx) {
    if (!n || stop_idx >= n->n_stops) return -1;
    return n->stop_pos[stop_idx];
}

const char *network_route_name(const NetworkModel *n, size_t r) {
    if (!n || r >= n->n_routes) return NULL;
    return n->routes[r].name;
}

int network_leg_route_id(const NetworkModel *n, size_t r, size_t k) {
    if (!n || r >= n->n_routes || k >= n->routes[r].stop_count) return -1;
    return (int)(r * 1000 + k);
}

const PathResult *network_leg_path(const NetworkModel *n, size_t r, size_t k) {
    if (!n || r >= n->n_routes || k >= n->routes[r].stop_count || !n->rm) return NULL;
    return rm_get_path(n->rm, (int)(r * 1000 + k));
}

double network_leg_time_s(const NetworkModel *n, size_t r, size_t k, CostMode mode) {
    const PathResult *path = network_leg_path(n, r, k);
    if (!path || !n->g) return -1.0;
    return path_time_s(n->g, path, mode);
}

double network_route_loop_time_s(const NetworkModel *n, size_t r, CostMode mode) {
    if (!n || r >= n->n_routes) return -1.0;
    size_t count = n->routes[r].stop_count;
    double total = 0.0;
    for (size_t k = 0; k < count; k++) {
        double t = network_leg_time_s(n, r, k, mode);
        if (t < 0.0) return -1.0;
        total += t;
    }
    return total;
}

double network_route_cycle_s(const NetworkModel *n, size_t r) {
    if (!n || r >= n->n_routes) return -1.0;
    double loop_t = network_route_loop_time_s(n, r, COST_STATIC);
    if (loop_t < 0.0) return -1.0;
    return loop_t + (double)n->routes[r].stop_count * n->dwell_s_per_stop;
}

Status network_route_cycles(const NetworkModel *n, double *out) {
    if (!n || !out) return STATUS_ERR_NULL;
    for (size_t r = 0; r < n->n_routes; r++) {
        double c = network_route_cycle_s(n, r);
        if (c < 0.0) return STATUS_ERR_INVALID;
        out[r] = c;
    }
    return STATUS_OK;
}

double network_stop_time_s(const NetworkModel *n, size_t a, size_t b) {
    if (!n || !n->matrix || a >= n->n_stops || b >= n->n_stops) return -1.0;
    return n->matrix[a * n->n_stops + b];
}

double network_entry_time_s(const NetworkModel *n, size_t from_stop, size_t to_route, int *entry_stop) {
    if (entry_stop) *entry_stop = -1;
    if (!n || !n->matrix || from_stop >= n->n_stops || to_route >= n->n_routes) {
        return -1.0;
    }
    size_t count = n->routes[to_route].stop_count;
    if (count == 0) return -1.0;

    double min_t = 1e30;
    int best_s = -1;

    for (size_t k = 0; k < count; k++) {
        int s = n->routes[to_route].tour[k];
        double t = network_stop_time_s(n, from_stop, (size_t)s);
        if (t < 0.0) continue;
        if (t < min_t || (t == min_t && s < best_s)) {
            min_t = t;
            best_s = s;
        }
    }

    if (best_s < 0) return -1.0;
    if (entry_stop) *entry_stop = best_s;
    return min_t;
}

Status network_route_demands(const NetworkModel *n, const DemandPredictor *dp, int hour, DayType day_type, int rain, const int *event_stops, size_t n_event_stops, double *out) {
    if (!n || !out) return STATUS_ERR_NULL;
    if (hour < 0 || hour > 23) return STATUS_ERR_INVALID;

    for (size_t r = 0; r < n->n_routes; r++) {
        double sum = 0.0;
        size_t count = n->routes[r].stop_count;
        for (size_t k = 0; k < count; k++) {
            int s_idx = n->routes[r].tour[k];
            const DemandStop *s = &n->stops[s_idx];
            if (!dp) {
                int is_event = 0;
                if (event_stops && n_event_stops > 0) {
                    for (size_t e = 0; e < n_event_stops; e++) {
                        if (event_stops[e] == s_idx) {
                            is_event = 1;
                            break;
                        }
                    }
                }
                double d = demand_expected(s->type, s->scale, hour, day_type, rain, is_event);
                if (d < 0.0) return STATUS_ERR_INVALID;
                sum += d;
            } else {
                double d = dp_predict(dp, s->node, s->type, s->scale, hour, day_type, rain, 0);
                if (d < 0.0) d = 0.0;
                sum += d;
            }
        }
        out[r] = sum;
    }
    return STATUS_OK;
}
