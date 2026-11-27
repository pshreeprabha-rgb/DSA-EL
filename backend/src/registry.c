/**
 * @file registry.c
 * @brief Entity registry implementation using Tree, HashTable, AVL, LinkedList, and Heap.
 */

#include "registry.h"
#include "tree.h"
#include "hashtable.h"
#include "avl.h"
#include "heap.h"
#include "linkedlist.h"
#include "graph.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct RegNodeData {
    int kind;
    int id;
    char name[64];
} RegNodeData;

struct Registry {
    const NetworkModel *net;
    size_t n_depots;
    size_t *depot_stops;

    size_t n_routes;
    size_t n_stops;
    size_t n_buses;

    Tree *tree;
    TreeNode **depot_nodes;
    TreeNode **route_nodes;
    TreeNode **bus_nodes;
    int *bus_parent_route; /* -1 = depot, >= 0 = route_id */
    int *bus_parent_depot; /* depot index when on depot, else -1 */
    int *bus_journey_seq;

    HashTable *buses_ht;
    HashTable *routes_ht;
    HashTable *stops_ht;

    Avl *timetable;
    int next_trip_id;
};

static RegNodeData *reg_node_data_create(int kind, int id, const char *name) {
    RegNodeData *d = (RegNodeData *)calloc(1, sizeof(RegNodeData));
    if (!d) return NULL;
    d->kind = kind;
    d->id = id;
    if (name) {
        snprintf(d->name, sizeof(d->name), "%s", name);
    }
    return d;
}

static size_t nearest_depot_for_stop(const Registry *r, size_t stop_idx) {
    const DemandStop *target = network_stop(r->net, stop_idx);
    if (!target || r->n_depots == 0) return 0;

    size_t best_d = 0;
    double best_dist = 1e30;
    for (size_t d = 0; d < r->n_depots; d++) {
        const DemandStop *ds = network_stop(r->net, r->depot_stops[d]);
        double dist = ds ? graph_haversine_m(target->lat, target->lng, ds->lat, ds->lng) : 1e30;
        if (d == 0 || dist < best_dist) {
            best_dist = dist;
            best_d = d;
        }
    }
    return best_d;
}

static void bus_destroy_cb(void *p) {
    bus_destroy((Bus *)p);
}

static void route_destroy_cb(void *p) {
    route_destroy((Route *)p);
}

static void stop_destroy_cb(void *p) {
    stop_destroy((Stop *)p);
}

static void trip_destroy_cb(void *p) {
    trip_destroy((Trip *)p);
}

static void journey_destroy_cb(void *p) {
    journey_record_destroy((JourneyRecord *)p);
}

static void update_route_assigned_counts(Registry *r) {
    for (size_t rt = 0; rt < r->n_routes; rt++) {
        Route *route = (Route *)ht_get_int(r->routes_ht, (int64_t)rt);
        if (route) {
            route->assigned_buses = 0;
        }
    }
    for (size_t b_idx = 0; b_idx < r->n_buses; b_idx++) {
        const Bus *b = (const Bus *)ht_get_int(r->buses_ht, (int64_t)b_idx);
        if (b && b->current_route_id >= 0 && (size_t)b->current_route_id < r->n_routes) {
            Route *route = (Route *)ht_get_int(r->routes_ht, (int64_t)b->current_route_id);
            if (route) {
                route->assigned_buses++;
            }
        }
    }
}

Status registry_create(const NetworkModel *net, const RegistryDepotSpec *depots, size_t n_depots,
                       const Sim *sim, Registry **out) {
    if (out) *out = NULL;
    if (!net || !depots || !sim || !out) return STATUS_ERR_NULL;
    if (n_depots == 0) return STATUS_ERR_INVALID;

    size_t n_stops = network_stop_count(net);
    size_t n_routes = network_route_count(net);
    size_t n_buses = sim_bus_count(sim);
    if (n_stops == 0 || n_routes == 0) return STATUS_ERR_INVALID;

    for (size_t d = 0; d < n_depots; d++) {
        if (!depots[d].name) return STATUS_ERR_NULL;
        if (depots[d].stop_idx >= n_stops) return STATUS_ERR_INVALID;
    }

    Registry *r = (Registry *)calloc(1, sizeof(Registry));
    if (!r) return STATUS_ERR_NOMEM;

    r->net = net;
    r->n_depots = n_depots;
    r->n_routes = n_routes;
    r->n_stops = n_stops;
    r->n_buses = n_buses;
    r->next_trip_id = 1;

    r->depot_stops = (size_t *)malloc(n_depots * sizeof(size_t));
    r->depot_nodes = (TreeNode **)calloc(n_depots, sizeof(TreeNode *));
    r->route_nodes = (TreeNode **)calloc(n_routes, sizeof(TreeNode *));
    r->bus_nodes = n_buses > 0 ? (TreeNode **)calloc(n_buses, sizeof(TreeNode *)) : NULL;
    r->bus_parent_route = n_buses > 0 ? (int *)malloc(n_buses * sizeof(int)) : NULL;
    r->bus_parent_depot = n_buses > 0 ? (int *)malloc(n_buses * sizeof(int)) : NULL;
    r->bus_journey_seq = n_buses > 0 ? (int *)calloc(n_buses, sizeof(int)) : NULL;

    if (!r->depot_stops || !r->depot_nodes || !r->route_nodes ||
        (n_buses > 0 && (!r->bus_nodes || !r->bus_parent_route || !r->bus_parent_depot || !r->bus_journey_seq))) {
        registry_destroy(r);
        return STATUS_ERR_NOMEM;
    }
    for (size_t d = 0; d < n_depots; d++) {
        r->depot_stops[d] = depots[d].stop_idx;
    }

    r->buses_ht = ht_create(HT_KEY_INT, bus_destroy_cb);
    r->routes_ht = ht_create(HT_KEY_INT, route_destroy_cb);
    r->stops_ht = ht_create(HT_KEY_INT, stop_destroy_cb);
    r->timetable = avl_create(trip_cmp, trip_destroy_cb);
    if (!r->buses_ht || !r->routes_ht || !r->stops_ht || !r->timetable) {
        registry_destroy(r);
        return STATUS_ERR_NOMEM;
    }

    /* Populate stops hash table */
    for (size_t s = 0; s < n_stops; s++) {
        const DemandStop *ds = network_stop(net, s);
        if (!ds) {
            registry_destroy(r);
            return STATUS_ERR_INVALID;
        }
        Stop *st = stop_create((int)s, ds->name, ds->lat, ds->lng, ds->type);
        if (!st || ht_put_int(r->stops_ht, (int64_t)s, st) != STATUS_OK) {
            stop_destroy(st);
            registry_destroy(r);
            return STATUS_ERR_NOMEM;
        }
    }

    /* Populate routes hash table */
    for (size_t rt = 0; rt < n_routes; rt++) {
        Route *route = route_create((int)rt, network_route_name(net, rt), 0.0);
        if (!route) {
            registry_destroy(r);
            return STATUS_ERR_NOMEM;
        }
        size_t scnt = network_route_stop_count(net, rt);
        for (size_t k = 0; k < scnt; k++) {
            int sidx = network_route_stop(net, rt, k);
            if (route_add_stop(route, sidx) != STATUS_OK) {
                route_destroy(route);
                registry_destroy(r);
                return STATUS_ERR_NOMEM;
            }
        }
        if (ht_put_int(r->routes_ht, (int64_t)rt, route) != STATUS_OK) {
            route_destroy(route);
            registry_destroy(r);
            return STATUS_ERR_NOMEM;
        }
    }

    /* Build tree root ("Bengaluru") and depot children */
    RegNodeData *root_data = reg_node_data_create(REG_KIND_CITY, 0, "Bengaluru");
    if (!root_data) {
        registry_destroy(r);
        return STATUS_ERR_NOMEM;
    }
    r->tree = tree_create(root_data, free);
    if (!r->tree) {
        free(root_data);
        registry_destroy(r);
        return STATUS_ERR_NOMEM;
    }
    TreeNode *root_node = tree_root(r->tree);

    for (size_t d = 0; d < n_depots; d++) {
        RegNodeData *dd = reg_node_data_create(REG_KIND_DEPOT, (int)d, depots[d].name);
        if (!dd) {
            registry_destroy(r);
            return STATUS_ERR_NOMEM;
        }
        r->depot_nodes[d] = tree_add_child(r->tree, root_node, dd);
        if (!r->depot_nodes[d]) {
            free(dd);
            registry_destroy(r);
            return STATUS_ERR_NOMEM;
        }
    }

    /* Attach route nodes to depot nearest their first tour stop */
    for (size_t rt = 0; rt < n_routes; rt++) {
        int first_stop = network_route_stop(net, rt, 0);
        size_t best_d = (first_stop >= 0) ? nearest_depot_for_stop(r, (size_t)first_stop) : 0;
        const char *rname = network_route_name(net, rt);
        RegNodeData *rd = reg_node_data_create(REG_KIND_ROUTE, (int)rt, rname ? rname : "Route");
        if (!rd) {
            registry_destroy(r);
            return STATUS_ERR_NOMEM;
        }
        r->route_nodes[rt] = tree_add_child(r->tree, r->depot_nodes[best_d], rd);
        if (!r->route_nodes[rt]) {
            free(rd);
            registry_destroy(r);
            return STATUS_ERR_NOMEM;
        }
    }

    /* Place buses in ascending bus_id order using sim_bus_info */
    for (size_t i = 0; i < n_buses; i++) {
        SimBusInfo bi;
        if (sim_bus_info(sim, i, &bi) != STATUS_OK) {
            registry_destroy(r);
            return STATUS_ERR_INVALID;
        }

        int eff_route = -1;
        BusStatus bstat = BUS_IDLE;
        if (bi.state == SIM_BUS_RUNNING) {
            eff_route = bi.route;
            bstat = BUS_RUNNING;
        } else if (bi.state == SIM_BUS_DEADHEAD) {
            SimBusMotion bm;
            if (sim_bus_motion(sim, i, &bm) == STATUS_OK && bm.route >= 0) {
                eff_route = bm.route;
            } else {
                eff_route = bi.route;
            }
            bstat = BUS_RUNNING;
        } else if (bi.state == SIM_BUS_BROKEN) {
            eff_route = -1;
            bstat = BUS_BROKEN;
        } else {
            eff_route = -1;
            bstat = BUS_IDLE;
        }

        size_t stop_i = (bi.stop_idx >= 0 && (size_t)bi.stop_idx < n_stops) ? (size_t)bi.stop_idx : 0;
        size_t depot_id = 0;
        TreeNode *parent_node = NULL;
        if (eff_route >= 0 && (size_t)eff_route < n_routes) {
            int first_stop = network_route_stop(net, (size_t)eff_route, 0);
            depot_id = (first_stop >= 0) ? nearest_depot_for_stop(r, (size_t)first_stop) : 0;
            parent_node = r->route_nodes[eff_route];
            r->bus_parent_route[i] = eff_route;
            r->bus_parent_depot[i] = -1;
        } else {
            depot_id = nearest_depot_for_stop(r, stop_i);
            parent_node = r->depot_nodes[depot_id];
            r->bus_parent_route[i] = -1;
            r->bus_parent_depot[i] = (int)depot_id;
        }

        char reg_str[BUS_REG_MAX];
        snprintf(reg_str, sizeof(reg_str), "BUS-%04d", bi.id);
        Bus *bus = bus_create(bi.id, reg_str, 60, (int)depot_id);
        if (!bus) {
            registry_destroy(r);
            return STATUS_ERR_NOMEM;
        }
        bus->status = bstat;
        bus->current_route_id = eff_route;
        bus->load = bi.load;
        const DemandStop *ds = network_stop(net, stop_i);
        if (ds) {
            bus->lat = ds->lat;
            bus->lng = ds->lng;
        }
        if (ht_put_int(r->buses_ht, (int64_t)bi.id, bus) != STATUS_OK) {
            bus_destroy(bus);
            registry_destroy(r);
            return STATUS_ERR_NOMEM;
        }

        char bname[64];
        snprintf(bname, sizeof(bname), "Bus %d", bi.id);
        RegNodeData *bd = reg_node_data_create(REG_KIND_BUS, bi.id, bname);
        if (!bd) {
            registry_destroy(r);
            return STATUS_ERR_NOMEM;
        }
        r->bus_nodes[i] = tree_add_child(r->tree, parent_node, bd);
        if (!r->bus_nodes[i]) {
            free(bd);
            registry_destroy(r);
            return STATUS_ERR_NOMEM;
        }
    }

    update_route_assigned_counts(r);
    *out = r;
    return STATUS_OK;
}

void registry_destroy(Registry *r) {
    if (!r) return;
    tree_destroy(r->tree);
    ht_destroy(r->buses_ht);
    ht_destroy(r->routes_ht);
    ht_destroy(r->stops_ht);
    avl_destroy(r->timetable);
    free(r->depot_stops);
    free(r->depot_nodes);
    free(r->route_nodes);
    free(r->bus_nodes);
    free(r->bus_parent_route);
    free(r->bus_parent_depot);
    free(r->bus_journey_seq);
    free(r);
}

Status registry_sync(Registry *r, const Sim *sim) {
    if (!r || !sim) return STATUS_ERR_NULL;
    if (sim_bus_count(sim) != r->n_buses) return STATUS_ERR_INVALID;

    for (size_t i = 0; i < r->n_buses; i++) {
        SimBusInfo bi;
        if (sim_bus_info(sim, i, &bi) != STATUS_OK) {
            return STATUS_ERR_INVALID;
        }

        Bus *bus = (Bus *)ht_get_int(r->buses_ht, (int64_t)bi.id);
        if (!bus) return STATUS_ERR_NOT_FOUND;

        int eff_route = -1;
        BusStatus bstat = BUS_IDLE;
        if (bi.state == SIM_BUS_RUNNING) {
            eff_route = bi.route;
            bstat = BUS_RUNNING;
        } else if (bi.state == SIM_BUS_DEADHEAD) {
            SimBusMotion bm;
            if (sim_bus_motion(sim, i, &bm) == STATUS_OK && bm.route >= 0) {
                eff_route = bm.route;
            } else {
                eff_route = bi.route;
            }
            bstat = BUS_RUNNING;
        } else if (bi.state == SIM_BUS_BROKEN) {
            /* A broken bus keeps its last route */
            eff_route = bus->current_route_id;
            bstat = BUS_BROKEN;
        } else {
            eff_route = -1;
            bstat = BUS_IDLE;
        }

        size_t stop_i = (bi.stop_idx >= 0 && (size_t)bi.stop_idx < r->n_stops) ? (size_t)bi.stop_idx : 0;
        const DemandStop *ds = network_stop(r->net, stop_i);

        bus->status = bstat;
        bus->current_route_id = eff_route;
        bus->load = bi.load;
        if (ds) {
            bus->lat = ds->lat;
            bus->lng = ds->lng;
        }

        int new_route = -1;
        int new_depot = -1;
        TreeNode *new_parent = NULL;
        if (eff_route >= 0 && (size_t)eff_route < r->n_routes) {
            new_route = eff_route;
            new_parent = r->route_nodes[eff_route];
            int first_stop = network_route_stop(r->net, (size_t)eff_route, 0);
            bus->depot_id = (int)((first_stop >= 0) ? nearest_depot_for_stop(r, (size_t)first_stop) : 0);
        } else {
            size_t dep = nearest_depot_for_stop(r, stop_i);
            new_depot = (int)dep;
            new_parent = r->depot_nodes[dep];
            bus->depot_id = new_depot;
        }

        if (new_route != r->bus_parent_route[i] || new_depot != r->bus_parent_depot[i]) {
            char bname[64];
            snprintf(bname, sizeof(bname), "Bus %d", bi.id);
            RegNodeData *bd = reg_node_data_create(REG_KIND_BUS, bi.id, bname);
            if (!bd) return STATUS_ERR_NOMEM;

            Status st = tree_remove_subtree(r->tree, r->bus_nodes[i]);
            if (st != STATUS_OK) {
                free(bd);
                return st;
            }
            TreeNode *added = tree_add_child(r->tree, new_parent, bd);
            if (!added) {
                free(bd);
                r->bus_nodes[i] = NULL;
                return STATUS_ERR_NOMEM;
            }
            r->bus_nodes[i] = added;
            r->bus_parent_route[i] = new_route;
            r->bus_parent_depot[i] = new_depot;
        }
    }

    update_route_assigned_counts(r);
    return STATUS_OK;
}

const Bus *registry_bus(const Registry *r, int bus_id) {
    if (!r) return NULL;
    return (const Bus *)ht_get_int(r->buses_ht, (int64_t)bus_id);
}

const Route *registry_route(const Registry *r, int route_id) {
    if (!r) return NULL;
    return (const Route *)ht_get_int(r->routes_ht, (int64_t)route_id);
}

const Stop *registry_stop(const Registry *r, int stop_idx) {
    if (!r) return NULL;
    return (const Stop *)ht_get_int(r->stops_ht, (int64_t)stop_idx);
}

size_t registry_bus_count(const Registry *r) {
    return r ? r->n_buses : 0;
}

size_t registry_tree_count(const Registry *r) {
    return r ? tree_count(r->tree) : 0;
}

int registry_tree_height(const Registry *r) {
    return r ? (int)tree_height(r->tree) : -1;
}

static void collect_tree_rows(const TreeNode *node, int depth, int parent_row,
                              RegTreeRow *out, size_t max, size_t *n) {
    if (!node || *n >= max) return;
    const RegNodeData *d = (const RegNodeData *)tree_node_data(node);
    int my_row = (int)(*n);
    out[my_row].depth = depth;
    out[my_row].kind = d->kind;
    out[my_row].id = d->id;
    out[my_row].parent_row = parent_row;
    snprintf(out[my_row].name, sizeof(out[my_row].name), "%s", d->name);
    (*n)++;

    for (const TreeNode *ch = tree_node_first_child(node); ch && *n < max; ch = tree_node_next_sibling(ch)) {
        collect_tree_rows(ch, depth + 1, my_row, out, max, n);
    }
}

Status registry_tree_rows(const Registry *r, RegTreeRow *out, size_t max, size_t *n) {
    if (!r || !out || !n) return STATUS_ERR_NULL;
    *n = 0;
    collect_tree_rows(tree_root(r->tree), 0, -1, out, max, n);
    return STATUS_OK;
}

static Status generate_trips_range(Registry *r, const int *buses_per_route, size_t n_routes,
                                   int from_min, int to_min) {
    int *route_buses = r->n_buses > 0 ? (int *)malloc(r->n_buses * sizeof(int)) : NULL;
    if (r->n_buses > 0 && !route_buses) return STATUS_ERR_NOMEM;

    for (size_t rt = 0; rt < n_routes; rt++) {
        int c = buses_per_route[rt];
        if (c == 0) continue;

        double cycle_s = network_route_cycle_s(r->net, rt);
        int cycle_min = (int)floor(cycle_s / 60.0 + 0.5);
        if (cycle_min < 1) cycle_min = 1;

        size_t cnt = 0;
        for (size_t i = 0; i < r->n_buses; i++) {
            const Bus *b = (const Bus *)ht_get_int(r->buses_ht, (int64_t)i);
            if (b && b->current_route_id == (int)rt && b->status != BUS_BROKEN) {
                route_buses[cnt++] = b->id;
            }
        }

        double headway = (double)cycle_min / (double)c;
        for (int j = 0; j < c; j++) {
            int offset_j = (int)floor((double)j * headway + 0.5);
            int bus_id = ((size_t)j < cnt) ? route_buses[j] : -1;
            for (int dep = from_min + offset_j; dep < to_min; dep += cycle_min) {
                Trip *tr = trip_create(r->next_trip_id++, (int)rt, bus_id, dep);
                if (!tr) {
                    free(route_buses);
                    return STATUS_ERR_NOMEM;
                }
                Status st = avl_insert(r->timetable, tr);
                if (st != STATUS_OK) {
                    trip_destroy(tr);
                    free(route_buses);
                    return st;
                }
            }
        }
    }

    free(route_buses);
    return STATUS_OK;
}

static Status validate_timetable_args(const Registry *r, const int *buses_per_route, size_t n_routes,
                                      int from_min, int to_min) {
    if (!r || !buses_per_route) return STATUS_ERR_NULL;
    if (n_routes != r->n_routes) return STATUS_ERR_INVALID;
    if (from_min < 0 || to_min > 1440 || from_min >= to_min) return STATUS_ERR_INVALID;
    for (size_t i = 0; i < n_routes; i++) {
        if (buses_per_route[i] < 0) return STATUS_ERR_INVALID;
    }
    return STATUS_OK;
}

Status registry_build_timetable(Registry *r, const int *buses_per_route, size_t n_routes,
                                int from_min, int to_min) {
    Status vst = validate_timetable_args(r, buses_per_route, n_routes, from_min, to_min);
    if (vst != STATUS_OK) return vst;

    avl_destroy(r->timetable);
    r->timetable = avl_create(trip_cmp, trip_destroy_cb);
    if (!r->timetable) return STATUS_ERR_NOMEM;
    r->next_trip_id = 1;

    return generate_trips_range(r, buses_per_route, n_routes, from_min, to_min);
}

typedef struct {
    int min_dep;
    Trip *buf;
    size_t cap;
    size_t count;
} ReplanCollect;

static void collect_trips_ge_cb(void *item, void *ctx) {
    const Trip *tr = (const Trip *)item;
    ReplanCollect *rc = (ReplanCollect *)ctx;
    if (tr->departure_time >= rc->min_dep && rc->count < rc->cap) {
        rc->buf[rc->count++] = *tr;
    }
}

Status registry_replan_from(Registry *r, int now_min, const int *buses_per_route, size_t n_routes,
                            int to_min) {
    Status vst = validate_timetable_args(r, buses_per_route, n_routes, now_min, to_min);
    if (vst != STATUS_OK) return vst;

    size_t cur_sz = avl_size(r->timetable);
    if (cur_sz > 0) {
        Trip *to_del = (Trip *)malloc(cur_sz * sizeof(Trip));
        if (!to_del) return STATUS_ERR_NOMEM;

        ReplanCollect rc = {now_min, to_del, cur_sz, 0};
        avl_inorder(r->timetable, collect_trips_ge_cb, &rc);

        for (size_t i = 0; i < rc.count; i++) {
            avl_delete(r->timetable, &to_del[i]);
        }
        free(to_del);
    }

    return generate_trips_range(r, buses_per_route, n_routes, now_min, to_min);
}

const Trip *registry_next_departure(const Registry *r, int route_id, int after_min) {
    if (!r || route_id < 0 || (size_t)route_id >= r->n_routes) return NULL;
    Trip probe;
    probe.departure_time = after_min;
    probe.route_id = route_id;
    probe.bus_id = -1;
    probe.trip_id = 0;

    for (;;) {
        const Trip *found = (const Trip *)avl_lower_bound(r->timetable, &probe);
        if (!found) return NULL;
        if (found->route_id == route_id) return found;
        probe.departure_time = found->departure_time;
        probe.trip_id = found->trip_id + 1;
    }
}

typedef struct {
    int from_min;
    int to_min;
    int route_id;
    Trip *out;
    size_t max;
    size_t count;
} TripQueryCtx;

static void collect_trips_range_cb(void *item, void *ctx) {
    const Trip *tr = (const Trip *)item;
    TripQueryCtx *q = (TripQueryCtx *)ctx;
    if (q->count >= q->max) return;
    if (tr->departure_time >= q->from_min && tr->departure_time < q->to_min) {
        if (q->route_id == -1 || tr->route_id == q->route_id) {
            q->out[q->count++] = *tr;
        }
    }
}

Status registry_trips(const Registry *r, int from_min, int to_min, int route_id,
                      Trip *out, size_t max, size_t *n) {
    if (!r || !out || !n) return STATUS_ERR_NULL;
    if (from_min < 0 || to_min > 1440 || from_min >= to_min) return STATUS_ERR_INVALID;
    if (route_id < -1 || route_id >= (int)r->n_routes) return STATUS_ERR_INVALID;

    TripQueryCtx q = {from_min, to_min, route_id, out, max, 0};
    avl_inorder(r->timetable, collect_trips_range_cb, &q);
    *n = q.count;
    return STATUS_OK;
}

size_t registry_trip_count(const Registry *r) {
    return r ? avl_size(r->timetable) : 0;
}

int registry_timetable_height(const Registry *r) {
    return r ? avl_height(r->timetable) : -1;
}

int registry_timetable_balanced(const Registry *r) {
    return r ? avl_is_balanced(r->timetable) : -1;
}

void registry_sim_observer(void *ctx, int bus, int route, double start_s, double end_s, int pax) {
    if (!ctx) return;
    Registry *r = (Registry *)ctx;
    Bus *b = (Bus *)ht_get_int(r->buses_ht, (int64_t)bus);
    if (!b || !b->journey) return;
    if (bus < 0 || (size_t)bus >= r->n_buses) return;

    int seq = ++r->bus_journey_seq[bus];
    JourneyRecord *jr = journey_record_create(seq, route, start_s / 60.0, end_s / 60.0, pax);
    if (!jr) return;

    while (ll_size(b->journey) >= REGISTRY_MAX_JOURNEYS) {
        ll_remove_at(b->journey, 0);
    }
    if (ll_append(b->journey, jr) != STATUS_OK) {
        journey_destroy_cb(jr);
    }
}

typedef struct {
    JourneyRecord *out;
    size_t max;
    size_t count;
} JourneyCopyCtx;

static void copy_journey_cb(void *item, void *ctx) {
    const JourneyRecord *jr = (const JourneyRecord *)item;
    JourneyCopyCtx *jc = (JourneyCopyCtx *)ctx;
    if (jc->count < jc->max) {
        jc->out[jc->count++] = *jr;
    }
}

Status registry_journeys(const Registry *r, int bus_id, JourneyRecord *out, size_t max, size_t *n) {
    if (!r || !out || !n) return STATUS_ERR_NULL;
    *n = 0;

    const Bus *b = registry_bus(r, bus_id);
    if (!b) return STATUS_ERR_NOT_FOUND;

    JourneyCopyCtx jc = {out, max, 0};
    ll_foreach(b->journey, copy_journey_cb, &jc);
    *n = jc.count;
    return STATUS_OK;
}

static int board_route_cmp_max(const void *a, const void *b) {
    const Route *ra = (const Route *)a;
    const Route *rb = (const Route *)b;
    if (ra->priority > rb->priority) return -1;
    if (ra->priority < rb->priority) return 1;
    if (ra->id < rb->id) return -1;
    if (ra->id > rb->id) return 1;
    return 0;
}

Status registry_route_board(const Registry *r, const double *demand, const int *counts,
                            RouteBoardRow *out, size_t n) {
    if (!r || !demand || !counts || !out) return STATUS_ERR_NULL;
    if (n != r->n_routes) return STATUS_ERR_INVALID;

    for (size_t i = 0; i < n; i++) {
        if (!isfinite(demand[i]) || demand[i] < 0.0 || counts[i] < 0) {
            return STATUS_ERR_INVALID;
        }
    }

    Heap *heap = heap_create(board_route_cmp_max, route_destroy_cb, NULL);
    if (!heap) return STATUS_ERR_NOMEM;

    for (size_t i = 0; i < n; i++) {
        Route *rt = route_create((int)i, network_route_name(r->net, i), demand[i]);
        if (!rt) {
            heap_destroy(heap);
            return STATUS_ERR_NOMEM;
        }
        rt->assigned_buses = counts[i];
        rt->priority = route_compute_priority(rt);
        if (heap_push(heap, rt) != STATUS_OK) {
            route_destroy(rt);
            heap_destroy(heap);
            return STATUS_ERR_NOMEM;
        }
    }

    for (size_t i = 0; i < n; i++) {
        Route *rt = (Route *)heap_pop(heap);
        out[i].route_id = rt->id;
        out[i].demand = rt->predicted_demand;
        out[i].buses = rt->assigned_buses;
        out[i].priority = rt->priority;
        route_destroy(rt);
    }

    heap_destroy(heap);
    return STATUS_OK;
}

