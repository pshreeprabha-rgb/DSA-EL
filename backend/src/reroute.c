#define _POSIX_C_SOURCE 200809L

#include "reroute.h"
#include "hashtable.h"
#include "stack.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- Internal types ---- */

typedef struct TrackedRoute {
    int route_id;
    int src;
    int dst;
    PathResult path;
} TrackedRoute;

typedef struct EdgeChange {
    int edge_id;
    double old_factor;
    int old_blocked;
} EdgeChange;

typedef struct RouteChange {
    int route_id;
    PathResult old_path;
} RouteChange;

typedef struct ChangeSet {
    char description[128];
    EdgeChange *edge_changes;
    size_t edge_change_count;
    RouteChange *route_changes;
    size_t route_change_count;
} ChangeSet;

struct RouteManager {
    Graph *g;
    double threshold;
    HashTable *routes;        /* int64_t route_id -> TrackedRoute* */
    Stack *history;           /* Stack<ChangeSet*> */
    int *route_id_list;       /* parallel list for iteration */
    size_t route_id_count;
    size_t route_id_capacity;
};

/* ---- Cleanup helpers ---- */

static void changeset_free(void *ptr) {
    if (!ptr) return;
    ChangeSet *cs = (ChangeSet *)ptr;
    free(cs->edge_changes);
    for (size_t i = 0; i < cs->route_change_count; i++) {
        path_result_free(&cs->route_changes[i].old_path);
    }
    free(cs->route_changes);
    free(cs);
}

static void tracked_route_free(void *ptr) {
    if (!ptr) return;
    TrackedRoute *tr = (TrackedRoute *)ptr;
    path_result_free(&tr->path);
    free(tr);
}

/* ---- Route ID list helpers ---- */

static Status rm_add_route_id(RouteManager *rm, int route_id) {
    if (rm->route_id_count >= rm->route_id_capacity) {
        size_t nc = (rm->route_id_capacity == 0) ? 8 : rm->route_id_capacity * 2;
        int *na = (int *)realloc(rm->route_id_list, nc * sizeof(int));
        if (!na) return STATUS_ERR_NOMEM;
        rm->route_id_list = na;
        rm->route_id_capacity = nc;
    }
    rm->route_id_list[rm->route_id_count++] = route_id;
    return STATUS_OK;
}

static void rm_remove_route_id(RouteManager *rm, int route_id) {
    for (size_t i = 0; i < rm->route_id_count; i++) {
        if (rm->route_id_list[i] == route_id) {
            rm->route_id_list[i] = rm->route_id_list[--rm->route_id_count];
            return;
        }
    }
}

/* qsort comparator for int */
static int int_cmp(const void *a, const void *b) {
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

/* Returns sorted copy of route IDs; caller frees. count_out may be 0. */
static int *get_sorted_ids(RouteManager *rm, size_t *count_out) {
    *count_out = rm->route_id_count;
    if (rm->route_id_count == 0) return NULL;
    int *ids = (int *)malloc(rm->route_id_count * sizeof(int));
    if (!ids) { *count_out = 0; return NULL; }
    memcpy(ids, rm->route_id_list, rm->route_id_count * sizeof(int));
    qsort(ids, rm->route_id_count, sizeof(int), int_cmp);
    return ids;
}

/* ---- RouteChange array helpers ---- */

static int rc_append(RouteChange **arr, size_t *count, size_t *cap, TrackedRoute *tr) {
    if (*count >= *cap) {
        size_t nc = (*cap == 0) ? 4 : *cap * 2;
        RouteChange *na = (RouteChange *)realloc(*arr, nc * sizeof(RouteChange));
        if (!na) return 0;
        *arr = na;
        *cap = nc;
    }
    RouteChange *rc = &(*arr)[*count];
    rc->route_id = tr->route_id;
    path_result_init(&rc->old_path);
    if (path_result_copy(&rc->old_path, &tr->path) != STATUS_OK) return 0;
    (*count)++;
    return 1;
}

static void rc_free_all(RouteChange *arr, size_t count) {
    for (size_t i = 0; i < count; i++) {
        path_result_free(&arr[i].old_path);
    }
    free(arr);
}

/* ---- Re-planner ---- */

/*
 * Re-plans a route. If path changed, appends to rc_arr and updates sum.
 * Returns 1 if changed, 0 otherwise.
 */
static int replan_route(RouteManager *rm, TrackedRoute *tr,
                        RouteChange **rc_arr, size_t *rc_count, size_t *rc_cap,
                        RerouteSummary *sum) {
    PathResult new_path;
    path_result_init(&new_path);
    astar_find_path(rm->g, tr->src, tr->dst, COST_TRAFFIC, &new_path);

    int adopt = 0;

    if (!new_path.found) {
        if (tr->path.found) {
            /* Became unreachable */
            adopt = 1;
            if (sum) sum->routes_unreachable++;
        }
        /* else still unreachable: no change */
    } else {
        /* new_path found */
        if (!tr->path.found) {
            /* Was unreachable, now reachable */
            adopt = 1;
            if (sum) sum->routes_rerouted++;
        } else if (!path_is_valid(rm->g, &tr->path)) {
            /* Old path is invalid (e.g. contains blocked edge) */
            adopt = 1;
            if (sum) sum->routes_rerouted++;
        } else {
            double old_time = path_time_s(rm->g, &tr->path, COST_TRAFFIC);
            if (old_time < 0.0 || new_path.total_time_s < old_time - 1e-6) {
                adopt = 1;
                if (sum) sum->routes_rerouted++;
            }
        }
    }

    if (!adopt) {
        path_result_free(&new_path);
        return 0;
    }

    if (!rc_append(rc_arr, rc_count, rc_cap, tr)) {
        path_result_free(&new_path);
        return 0;
    }

    path_result_free(&tr->path);
    tr->path = new_path;
    return 1;
}

/* ---- Push history ---- */

static Status push_changeset(RouteManager *rm, const char *desc,
                             EdgeChange *ec, size_t ec_count,
                             RouteChange *rc, size_t rc_count) {
    ChangeSet *cs = (ChangeSet *)calloc(1, sizeof(ChangeSet));
    if (!cs) return STATUS_ERR_NOMEM;

    strncpy(cs->description, desc, sizeof(cs->description) - 1);
    cs->edge_changes = ec;
    cs->edge_change_count = ec_count;
    cs->route_changes = rc;
    cs->route_change_count = rc_count;

    Status st = stack_push(rm->history, cs);
    if (st != STATUS_OK) {
        /* Don't double-free ec/rc — caller owns them on failure */
        cs->edge_changes = NULL;
        cs->route_changes = NULL;
        free(cs);
        return st;
    }
    return STATUS_OK;
}

/* ---- Public API ---- */

RouteManager *rm_create(Graph *g, double reroute_threshold) {
    if (!g) return NULL;
    RouteManager *rm = (RouteManager *)calloc(1, sizeof(RouteManager));
    if (!rm) return NULL;

    rm->g = g;
    rm->threshold = (reroute_threshold <= 1.0) ? RM_DEFAULT_THRESHOLD : reroute_threshold;
    rm->routes = ht_create(HT_KEY_INT, tracked_route_free);
    rm->history = stack_create(changeset_free);

    if (!rm->routes || !rm->history) {
        ht_destroy(rm->routes);
        stack_destroy(rm->history);
        free(rm);
        return NULL;
    }
    return rm;
}

void rm_destroy(RouteManager *rm) {
    if (!rm) return;
    ht_destroy(rm->routes);
    stack_destroy(rm->history);
    free(rm->route_id_list);
    free(rm);
}

Status rm_track_route(RouteManager *rm, int route_id, int src, int dst) {
    if (!rm) return STATUS_ERR_NULL;
    if (ht_contains_int(rm->routes, (int64_t)route_id)) return STATUS_ERR_DUPLICATE;

    TrackedRoute *tr = (TrackedRoute *)calloc(1, sizeof(TrackedRoute));
    if (!tr) return STATUS_ERR_NOMEM;

    tr->route_id = route_id;
    tr->src = src;
    tr->dst = dst;
    path_result_init(&tr->path);

    Status st = astar_find_path(rm->g, src, dst, COST_TRAFFIC, &tr->path);
    if (st != STATUS_OK) { free(tr); return st; }

    st = ht_put_int(rm->routes, (int64_t)route_id, tr);
    if (st != STATUS_OK) { tracked_route_free(tr); return st; }

    st = rm_add_route_id(rm, route_id);
    if (st != STATUS_OK) {
        ht_remove_int(rm->routes, (int64_t)route_id);
        return st;
    }
    return STATUS_OK;
}

Status rm_untrack_route(RouteManager *rm, int route_id) {
    if (!rm) return STATUS_ERR_NULL;
    if (!ht_contains_int(rm->routes, (int64_t)route_id)) return STATUS_ERR_NOT_FOUND;
    rm_remove_route_id(rm, route_id);
    return ht_remove_int(rm->routes, (int64_t)route_id);
}

const PathResult *rm_get_path(const RouteManager *rm, int route_id) {
    if (!rm) return NULL;
    TrackedRoute *tr = (TrackedRoute *)ht_get_int(rm->routes, (int64_t)route_id);
    return tr ? &tr->path : NULL;
}

Status rm_block_edge(RouteManager *rm, int edge_id, RerouteSummary *out) {
    if (!rm) return STATUS_ERR_NULL;
    RerouteSummary sum = {0, 0, 0, 0};

    const Edge *e = graph_edge(rm->g, edge_id);
    if (!e) return STATUS_ERR_INVALID;

    if (e->blocked) {
        if (out) *out = sum;
        return STATUS_OK;
    }

    EdgeChange *ec = (EdgeChange *)malloc(sizeof(EdgeChange));
    if (!ec) return STATUS_ERR_NOMEM;
    ec->edge_id = edge_id;
    ec->old_factor = e->traffic_factor;
    ec->old_blocked = e->blocked;

    graph_set_blocked(rm->g, edge_id, 1);

    size_t id_count;
    int *ids = get_sorted_ids(rm, &id_count);

    RouteChange *rc_arr = NULL;
    size_t rc_count = 0, rc_cap = 0;

    for (size_t i = 0; i < id_count; i++) {
        TrackedRoute *tr = (TrackedRoute *)ht_get_int(rm->routes, (int64_t)ids[i]);
        if (!tr) continue;
        /* Only reroute routes that contain this edge */
        if (tr->path.found && !path_contains_edge(&tr->path, edge_id)) continue;
        sum.routes_checked++;
        replan_route(rm, tr, &rc_arr, &rc_count, &rc_cap, &sum);
    }
    free(ids);

    char desc[128];
    snprintf(desc, sizeof(desc), "block edge %d", edge_id);
    Status st = push_changeset(rm, desc, ec, 1, rc_arr, rc_count);
    if (st == STATUS_OK) {
        sum.pushed_history = 1;
    } else {
        free(ec);
        rc_free_all(rc_arr, rc_count);
    }

    if (out) *out = sum;
    return st;
}

Status rm_unblock_edge(RouteManager *rm, int edge_id, RerouteSummary *out) {
    if (!rm) return STATUS_ERR_NULL;
    RerouteSummary sum = {0, 0, 0, 0};

    const Edge *e = graph_edge(rm->g, edge_id);
    if (!e) return STATUS_ERR_INVALID;

    if (!e->blocked) {
        if (out) *out = sum;
        return STATUS_OK;
    }

    EdgeChange *ec = (EdgeChange *)malloc(sizeof(EdgeChange));
    if (!ec) return STATUS_ERR_NOMEM;
    ec->edge_id = edge_id;
    ec->old_factor = e->traffic_factor;
    ec->old_blocked = e->blocked;

    graph_set_blocked(rm->g, edge_id, 0);

    size_t id_count;
    int *ids = get_sorted_ids(rm, &id_count);

    RouteChange *rc_arr = NULL;
    size_t rc_count = 0, rc_cap = 0;

    for (size_t i = 0; i < id_count; i++) {
        TrackedRoute *tr = (TrackedRoute *)ht_get_int(rm->routes, (int64_t)ids[i]);
        if (!tr) continue;
        /* Retry unreachable routes too */
        if (tr->path.found && !path_contains_edge(&tr->path, edge_id)) continue;
        sum.routes_checked++;
        replan_route(rm, tr, &rc_arr, &rc_count, &rc_cap, &sum);
    }
    free(ids);

    char desc[128];
    snprintf(desc, sizeof(desc), "unblock edge %d", edge_id);
    Status st = push_changeset(rm, desc, ec, 1, rc_arr, rc_count);
    if (st == STATUS_OK) {
        sum.pushed_history = 1;
    } else {
        free(ec);
        rc_free_all(rc_arr, rc_count);
    }

    if (out) *out = sum;
    return st;
}

Status rm_set_traffic_factor(RouteManager *rm, int edge_id, double factor, RerouteSummary *out) {
    if (!rm) return STATUS_ERR_NULL;
    RerouteSummary sum = {0, 0, 0, 0};

    const Edge *e = graph_edge(rm->g, edge_id);
    if (!e) return STATUS_ERR_INVALID;

    if (factor < 1.0) factor = 1.0;
    if (factor > GRAPH_MAX_TRAFFIC_FACTOR) factor = GRAPH_MAX_TRAFFIC_FACTOR;

    double old_factor = e->traffic_factor;
    int old_blocked = e->blocked;

    if (factor == old_factor) {
        if (out) *out = sum;
        return STATUS_OK;
    }

    EdgeChange *ec = (EdgeChange *)malloc(sizeof(EdgeChange));
    if (!ec) return STATUS_ERR_NOMEM;
    ec->edge_id = edge_id;
    ec->old_factor = old_factor;
    ec->old_blocked = old_blocked;

    graph_set_traffic_factor(rm->g, edge_id, factor);

    RouteChange *rc_arr = NULL;
    size_t rc_count = 0, rc_cap = 0;

    if (factor >= rm->threshold) {
        size_t id_count;
        int *ids = get_sorted_ids(rm, &id_count);

        for (size_t i = 0; i < id_count; i++) {
            TrackedRoute *tr = (TrackedRoute *)ht_get_int(rm->routes, (int64_t)ids[i]);
            if (!tr || !path_contains_edge(&tr->path, edge_id)) continue;
            sum.routes_checked++;
            replan_route(rm, tr, &rc_arr, &rc_count, &rc_cap, &sum);
        }
        free(ids);
    }

    char desc[128];
    snprintf(desc, sizeof(desc), "set traffic %.2f on edge %d", factor, edge_id);
    Status st = push_changeset(rm, desc, ec, 1, rc_arr, rc_count);
    if (st == STATUS_OK) {
        sum.pushed_history = 1;
    } else {
        free(ec);
        rc_free_all(rc_arr, rc_count);
    }

    if (out) *out = sum;
    return st;
}

Status rm_check_and_reroute(RouteManager *rm, RerouteSummary *out) {
    if (!rm) return STATUS_ERR_NULL;
    RerouteSummary sum = {0, 0, 0, 0};

    size_t id_count;
    int *ids = get_sorted_ids(rm, &id_count);

    RouteChange *rc_arr = NULL;
    size_t rc_count = 0, rc_cap = 0;

    for (size_t i = 0; i < id_count; i++) {
        TrackedRoute *tr = (TrackedRoute *)ht_get_int(rm->routes, (int64_t)ids[i]);
        if (!tr) continue;

        int affected = !tr->path.found;
        if (!affected && tr->path.edges) {
            for (size_t ei = 0; ei < tr->path.edge_count && !affected; ei++) {
                const Edge *e = graph_edge(rm->g, tr->path.edges[ei]);
                if (e && e->traffic_factor >= rm->threshold) affected = 1;
            }
        }
        if (!affected) continue;

        sum.routes_checked++;
        replan_route(rm, tr, &rc_arr, &rc_count, &rc_cap, &sum);
    }
    free(ids);

    if (rc_count == 0) {
        free(rc_arr);
        if (out) *out = sum;
        return STATUS_OK;
    }

    char desc[128];
    snprintf(desc, sizeof(desc), "check_and_reroute: %zu changed", rc_count);
    Status st = push_changeset(rm, desc, NULL, 0, rc_arr, rc_count);
    if (st == STATUS_OK) {
        sum.pushed_history = 1;
    } else {
        rc_free_all(rc_arr, rc_count);
    }

    if (out) *out = sum;
    return st;
}

Status rm_undo(RouteManager *rm, char *description, size_t desc_size) {
    if (!rm) return STATUS_ERR_NULL;
    if (stack_is_empty(rm->history)) return STATUS_ERR_EMPTY;

    ChangeSet *cs = (ChangeSet *)stack_pop(rm->history);
    if (!cs) return STATUS_ERR_EMPTY;

    if (description && desc_size > 0) {
        strncpy(description, cs->description, desc_size - 1);
        description[desc_size - 1] = '\0';
    }

    /* Restore edge states */
    for (size_t i = 0; i < cs->edge_change_count; i++) {
        EdgeChange *ec = &cs->edge_changes[i];
        graph_set_traffic_factor(rm->g, ec->edge_id, ec->old_factor);
        graph_set_blocked(rm->g, ec->edge_id, ec->old_blocked);
    }

    /* Restore route paths */
    for (size_t i = 0; i < cs->route_change_count; i++) {
        RouteChange *rc = &cs->route_changes[i];
        TrackedRoute *tr = (TrackedRoute *)ht_get_int(rm->routes, (int64_t)rc->route_id);
        if (tr) {
            path_result_free(&tr->path);
            path_result_copy(&tr->path, &rc->old_path);
        }
    }

    changeset_free(cs);
    return STATUS_OK;
}

size_t rm_history_size(const RouteManager *rm) {
    if (!rm) return 0;
    return stack_size(rm->history);
}

Status rm_history_describe(const RouteManager *rm, size_t index_from_top, char *buf, size_t buf_size) {
    if (!rm || !buf || buf_size == 0) return STATUS_ERR_NULL;
    ChangeSet *cs = (ChangeSet *)stack_get(rm->history, index_from_top);
    if (!cs) return STATUS_ERR_NOT_FOUND;
    strncpy(buf, cs->description, buf_size - 1);
    buf[buf_size - 1] = '\0';
    return STATUS_OK;
}
