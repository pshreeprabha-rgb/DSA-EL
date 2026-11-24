#include "fleet_alloc.h"
#include "heap.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* --- Equal Allocation --- */

Status fleet_targets_equal(size_t n_routes, size_t n_buses, int *out) {
    if (n_routes == 0 || out == NULL) {
        return (out == NULL) ? STATUS_ERR_NULL : STATUS_ERR_INVALID;
    }
    size_t base = n_buses / n_routes;
    size_t rem = n_buses % n_routes;
    for (size_t r = 0; r < n_routes; r++) {
        out[r] = (int)base + (r < rem ? 1 : 0);
    }
    return STATUS_OK;
}

/* --- Proportional Allocation (Hamilton largest-remainder) --- */

typedef struct {
    size_t route;
    double frac;
    int int_part;
} PropRouteItem;

static int prop_item_cmp(const void *pa, const void *pb) {
    const PropRouteItem *a = (const PropRouteItem *)pa;
    const PropRouteItem *b = (const PropRouteItem *)pb;
    if (a->frac != b->frac) {
        return (a->frac > b->frac) ? -1 : 1; /* descending frac */
    }
    return (a->route < b->route) ? -1 : 1;   /* ascending route ID */
}

Status fleet_targets_proportional(const double *weights, size_t n_routes, size_t n_buses,
                                  int min_per_route, int *out) {
    if (weights == NULL || out == NULL) {
        return STATUS_ERR_NULL;
    }
    if (n_routes == 0 || min_per_route < 0 || n_buses < (size_t)min_per_route * n_routes) {
        return STATUS_ERR_INVALID;
    }

    double sum_w = 0.0;
    for (size_t r = 0; r < n_routes; r++) {
        if (isnan(weights[r]) || isinf(weights[r]) || weights[r] < 0.0) {
            return STATUS_ERR_INVALID;
        }
        sum_w += weights[r];
        out[r] = min_per_route;
    }

    size_t remaining = n_buses - (size_t)min_per_route * n_routes;
    if (remaining == 0) {
        return STATUS_OK;
    }

    if (sum_w <= 0.0) {
        int *rem_out = (int *)calloc(n_routes, sizeof(int));
        if (!rem_out) return STATUS_ERR_NOMEM;
        Status st = fleet_targets_equal(n_routes, remaining, rem_out);
        if (st == STATUS_OK) {
            for (size_t r = 0; r < n_routes; r++) {
                out[r] += rem_out[r];
            }
        }
        free(rem_out);
        return st;
    }

    PropRouteItem *items = (PropRouteItem *)malloc(n_routes * sizeof(PropRouteItem));
    if (!items) return STATUS_ERR_NOMEM;

    size_t allocated_int = 0;
    for (size_t r = 0; r < n_routes; r++) {
        double quota = ((double)remaining * weights[r]) / sum_w;
        int int_part = (int)floor(quota);
        items[r].route = r;
        items[r].int_part = int_part;
        items[r].frac = quota - (double)int_part;
        allocated_int += (size_t)int_part;
        out[r] += int_part;
    }

    size_t leftover = remaining - allocated_int;
    qsort(items, n_routes, sizeof(PropRouteItem), prop_item_cmp);

    for (size_t k = 0; k < leftover && k < n_routes; k++) {
        out[items[k].route]++;
    }

    free(items);
    return STATUS_OK;
}

/* --- Greedy Allocation --- */

typedef struct {
    size_t route;
    int count;
    double priority;
} AllocHeapNode;

static int alloc_heap_cmp(const void *pa, const void *pb) {
    const AllocHeapNode *a = (const AllocHeapNode *)pa;
    const AllocHeapNode *b = (const AllocHeapNode *)pb;
    if (a->priority != b->priority) {
        return (a->priority > b->priority) ? -1 : 1; /* higher priority popped first */
    }
    if (a->count != b->count) {
        return (a->count < b->count) ? -1 : 1;       /* smaller count popped first */
    }
    return (a->route < b->route) ? -1 : 1;           /* smaller route ID popped first */
}

Status fleet_targets_greedy(const double *demand, const double *cycle_s,
                            size_t n_routes, size_t n_buses, int min_per_route,
                            FleetPriority prio, int *out) {
    if (demand == NULL || out == NULL) {
        return STATUS_ERR_NULL;
    }
    if (n_routes == 0 || min_per_route < 0 || n_buses < (size_t)min_per_route * n_routes) {
        return STATUS_ERR_INVALID;
    }
    if (prio == FLEET_PRIO_MARGINAL_WAIT) {
        if (min_per_route < 1 || cycle_s == NULL) {
            return (cycle_s == NULL) ? STATUS_ERR_NULL : STATUS_ERR_INVALID;
        }
    }

    for (size_t r = 0; r < n_routes; r++) {
        if (isnan(demand[r]) || isinf(demand[r]) || demand[r] < 0.0) {
            return STATUS_ERR_INVALID;
        }
        if (prio == FLEET_PRIO_MARGINAL_WAIT) {
            if (isnan(cycle_s[r]) || isinf(cycle_s[r]) || cycle_s[r] <= 0.0) {
                return STATUS_ERR_INVALID;
            }
        }
        out[r] = min_per_route;
    }

    AllocHeapNode *nodes = (AllocHeapNode *)malloc(n_routes * sizeof(AllocHeapNode));
    if (!nodes) return STATUS_ERR_NOMEM;

    Heap *h = heap_create(alloc_heap_cmp, NULL, NULL);
    if (!h) {
        free(nodes);
        return STATUS_ERR_NOMEM;
    }

    for (size_t r = 0; r < n_routes; r++) {
        nodes[r].route = r;
        nodes[r].count = min_per_route;
        if (prio == FLEET_PRIO_DEMAND_PER_BUS) {
            nodes[r].priority = demand[r] / (double)(min_per_route + 1);
        } else {
            nodes[r].priority = (demand[r] * cycle_s[r]) / ((double)min_per_route * (double)(min_per_route + 1));
        }
        Status st = heap_push(h, &nodes[r]);
        if (st != STATUS_OK) {
            heap_destroy(h);
            free(nodes);
            return st;
        }
    }

    size_t assigned = (size_t)min_per_route * n_routes;
    while (assigned < n_buses) {
        AllocHeapNode *best = (AllocHeapNode *)heap_pop(h);
        if (!best) {
            heap_destroy(h);
            free(nodes);
            return STATUS_ERR_INVALID;
        }
        best->count++;
        if (prio == FLEET_PRIO_DEMAND_PER_BUS) {
            best->priority = demand[best->route] / (double)(best->count + 1);
        } else {
            best->priority = (demand[best->route] * cycle_s[best->route]) /
                             ((double)best->count * (double)(best->count + 1));
        }
        Status st = heap_push(h, best);
        if (st != STATUS_OK) {
            heap_destroy(h);
            free(nodes);
            return st;
        }
        assigned++;
    }

    for (size_t r = 0; r < n_routes; r++) {
        out[r] = nodes[r].count;
    }
    heap_destroy(h);
    free(nodes);

    return STATUS_OK;
}

/* --- Waiting Time Proxy --- */

double fleet_wait_proxy_s(const double *demand, const double *cycle_s,
                          const int *counts, size_t n_routes) {
    if (demand == NULL || cycle_s == NULL || counts == NULL || n_routes == 0) {
        return NAN;
    }

    double total_demand = 0.0;
    for (size_t r = 0; r < n_routes; r++) {
        if (counts[r] < 0 || isnan(demand[r]) || isinf(demand[r]) || demand[r] < 0.0 ||
            isnan(cycle_s[r]) || isinf(cycle_s[r]) || cycle_s[r] < 0.0) {
            return NAN;
        }
        total_demand += demand[r];
    }

    if (total_demand <= 0.0) {
        return 0.0;
    }

    double sum_weighted_wait = 0.0;
    for (size_t r = 0; r < n_routes; r++) {
        double wait_r = 0.0;
        if (counts[r] > 0) {
            wait_r = cycle_s[r] / (2.0 * (double)counts[r]);
        } else if (demand[r] > 0.0) {
            wait_r = FLEET_NO_SERVICE_WAIT_S;
        } else {
            wait_r = 0.0;
        }
        sum_weighted_wait += demand[r] * wait_r;
    }

    return sum_weighted_wait / total_demand;
}

/* --- Nearest Bus Selection --- */

Status fleet_pick_nearest(const Network *net, const FleetBus *cands, size_t n,
                          size_t to_route, size_t *index_out, double *deadhead_s,
                          int *entry_stop) {
    if (net == NULL || cands == NULL || index_out == NULL || deadhead_s == NULL || entry_stop == NULL) {
        return STATUS_ERR_NULL;
    }
    if (n == 0 || to_route >= network_route_count(net)) {
        return STATUS_ERR_INVALID;
    }

    size_t best_idx = 0;
    double best_deadhead = 1e30;
    int best_entry = -1;
    int best_id = 2147483647;
    int found = 0;

    for (size_t i = 0; i < n; i++) {
        if (cands[i].stop_idx < 0) continue;
        int entry = -1;
        double dh = network_entry_time_s(net, (size_t)cands[i].stop_idx, to_route, &entry);
        if (dh < 0.0 || entry < 0) {
            continue;
        }
        if (!found || dh < best_deadhead || (dh == best_deadhead && cands[i].bus_id < best_id)) {
            best_deadhead = dh;
            best_idx = i;
            best_entry = entry;
            best_id = cands[i].bus_id;
            found = 1;
        }
    }

    if (!found) {
        return STATUS_ERR_NOT_FOUND;
    }

    *index_out = best_idx;
    *deadhead_s = best_deadhead;
    *entry_stop = best_entry;
    return STATUS_OK;
}

/* --- Move Planner --- */

typedef struct {
    size_t bus_index;
    int bus_id;
    int load;
} SurplusBusSortItem;

static int surplus_bus_cmp(const void *pa, const void *pb) {
    const SurplusBusSortItem *a = (const SurplusBusSortItem *)pa;
    const SurplusBusSortItem *b = (const SurplusBusSortItem *)pb;
    if (a->load != b->load) {
        return (a->load < b->load) ? -1 : 1; /* lowest load first */
    }
    return (a->bus_id < b->bus_id) ? -1 : 1; /* smaller bus_id first */
}

typedef struct {
    const FleetBus *bus;
    int original_route;
    int is_released;
    int assigned;
} MoverSlot;

typedef struct {
    size_t route;
    int satisfied;
} DeficitTargetSlot;

static int move_output_cmp(const void *pa, const void *pb) {
    const FleetMove *a = (const FleetMove *)pa;
    const FleetMove *b = (const FleetMove *)pb;
    return (a->bus_id < b->bus_id) ? -1 : 1;
}

Status fleet_plan_moves(const Network *net, const FleetBus *buses, size_t n_buses,
                        const int *targets, size_t n_routes,
                        FleetMove *moves_out, size_t max_moves, size_t *n_moves) {
    if (net == NULL || buses == NULL || targets == NULL || moves_out == NULL || n_moves == NULL) {
        return STATUS_ERR_NULL;
    }
    if (n_routes == 0 || n_routes != network_route_count(net)) {
        return STATUS_ERR_INVALID;
    }
    if (network_travel_time_s(net, 0, 0) < 0.0) {
        return STATUS_ERR_INVALID; /* matrix not built */
    }

    for (size_t r = 0; r < n_routes; r++) {
        if (targets[r] < 0) {
            return STATUS_ERR_INVALID;
        }
    }

    *n_moves = 0;

    int *current_counts = (int *)calloc(n_routes, sizeof(int));
    if (!current_counts) return STATUS_ERR_NOMEM;

    for (size_t i = 0; i < n_buses; i++) {
        int r = buses[i].route;
        if (r >= 0 && (size_t)r < n_routes) {
            current_counts[r]++;
        }
    }

    /* Track which buses are released */
    char *released_flag = (char *)calloc(n_buses, sizeof(char));
    if (!released_flag) {
        free(current_counts);
        return STATUS_ERR_NOMEM;
    }

    for (size_t r = 0; r < n_routes; r++) {
        if (current_counts[r] > targets[r]) {
            size_t surplus = (size_t)(current_counts[r] - targets[r]);
            /* Collect buses on route r */
            SurplusBusSortItem *sitems = (SurplusBusSortItem *)malloc(n_buses * sizeof(SurplusBusSortItem));
            if (!sitems) {
                free(released_flag);
                free(current_counts);
                return STATUS_ERR_NOMEM;
            }
            size_t scnt = 0;
            for (size_t i = 0; i < n_buses; i++) {
                if (buses[i].route == (int)r) {
                    sitems[scnt].bus_index = i;
                    sitems[scnt].bus_id = buses[i].bus_id;
                    sitems[scnt].load = buses[i].load;
                    scnt++;
                }
            }
            qsort(sitems, scnt, sizeof(SurplusBusSortItem), surplus_bus_cmp);
            for (size_t s = 0; s < surplus && s < scnt; s++) {
                released_flag[sitems[s].bus_index] = 1;
            }
            free(sitems);
        }
    }

    /* Build mover list: reserve buses (route == -1) + released buses */
    MoverSlot *movers = (MoverSlot *)malloc(n_buses * sizeof(MoverSlot));
    if (!movers) {
        free(released_flag);
        free(current_counts);
        return STATUS_ERR_NOMEM;
    }
    size_t n_movers = 0;
    for (size_t i = 0; i < n_buses; i++) {
        if (buses[i].route == -1) {
            movers[n_movers].bus = &buses[i];
            movers[n_movers].original_route = -1;
            movers[n_movers].is_released = 0;
            movers[n_movers].assigned = 0;
            n_movers++;
        } else if (released_flag[i]) {
            movers[n_movers].bus = &buses[i];
            movers[n_movers].original_route = buses[i].route;
            movers[n_movers].is_released = 1;
            movers[n_movers].assigned = 0;
            n_movers++;
        }
    }

    /* Build deficit target slots */
    size_t total_deficit = 0;
    for (size_t r = 0; r < n_routes; r++) {
        if (current_counts[r] < targets[r]) {
            total_deficit += (size_t)(targets[r] - current_counts[r]);
        }
    }

    DeficitTargetSlot *deficit_slots = NULL;
    if (total_deficit > 0) {
        deficit_slots = (DeficitTargetSlot *)malloc(total_deficit * sizeof(DeficitTargetSlot));
        if (!deficit_slots) {
            free(movers);
            free(released_flag);
            free(current_counts);
            return STATUS_ERR_NOMEM;
        }
        size_t d_idx = 0;
        for (size_t r = 0; r < n_routes; r++) {
            if (current_counts[r] < targets[r]) {
                size_t d = (size_t)(targets[r] - current_counts[r]);
                for (size_t k = 0; k < d; k++) {
                    deficit_slots[d_idx].route = r;
                    deficit_slots[d_idx].satisfied = 0;
                    d_idx++;
                }
            }
        }
    }

    /* Buffer to collect moves */
    FleetMove *temp_moves = (FleetMove *)malloc((n_buses + total_deficit + 1) * sizeof(FleetMove));
    if (!temp_moves) {
        free(deficit_slots);
        free(movers);
        free(released_flag);
        free(current_counts);
        return STATUS_ERR_NOMEM;
    }
    size_t move_count = 0;

    /* Match movers to deficit slots greedily */
    size_t unsatisfied_deficits = total_deficit;
    size_t available_movers = n_movers;

    while (unsatisfied_deficits > 0 && available_movers > 0) {
        double best_dh = 1e30;
        size_t best_m = 0;
        size_t best_d = 0;
        int best_entry = -1;
        int found = 0;

        for (size_t m = 0; m < n_movers; m++) {
            if (movers[m].assigned) continue;
            for (size_t d = 0; d < total_deficit; d++) {
                if (deficit_slots[d].satisfied) continue;

                if (movers[m].bus->stop_idx < 0) continue;
                int entry = -1;
                double dh = network_entry_time_s(net, (size_t)movers[m].bus->stop_idx, deficit_slots[d].route, &entry);
                if (dh < 0.0 || entry < 0) continue;

                if (!found) {
                    best_dh = dh;
                    best_m = m;
                    best_d = d;
                    best_entry = entry;
                    found = 1;
                } else if (dh < best_dh) {
                    best_dh = dh;
                    best_m = m;
                    best_d = d;
                    best_entry = entry;
                } else if (dh == best_dh) {
                    if (movers[m].bus->bus_id < movers[best_m].bus->bus_id) {
                        best_dh = dh;
                        best_m = m;
                        best_d = d;
                        best_entry = entry;
                    } else if (movers[m].bus->bus_id == movers[best_m].bus->bus_id) {
                        if (deficit_slots[d].route < deficit_slots[best_d].route) {
                            best_dh = dh;
                            best_m = m;
                            best_d = d;
                            best_entry = entry;
                        }
                    }
                }
            }
        }

        if (!found) {
            break; /* No reachable route for remaining movers */
        }

        /* Record move */
        temp_moves[move_count].bus_id = movers[best_m].bus->bus_id;
        temp_moves[move_count].from_route = movers[best_m].original_route;
        temp_moves[move_count].to_route = (int)deficit_slots[best_d].route;
        temp_moves[move_count].entry_stop = best_entry;
        temp_moves[move_count].deadhead_s = best_dh;
        move_count++;

        movers[best_m].assigned = 1;
        deficit_slots[best_d].satisfied = 1;
        unsatisfied_deficits--;
        available_movers--;
    }

    /* Unmatched released buses move to reserve (route = -1, entry_stop = -1, deadhead = 0.0) */
    for (size_t m = 0; m < n_movers; m++) {
        if (movers[m].is_released && !movers[m].assigned) {
            temp_moves[move_count].bus_id = movers[m].bus->bus_id;
            temp_moves[move_count].from_route = movers[m].original_route;
            temp_moves[move_count].to_route = -1;
            temp_moves[move_count].entry_stop = -1;
            temp_moves[move_count].deadhead_s = 0.0;
            move_count++;
            movers[m].assigned = 1;
        }
    }

    /* Sort moves by bus_id ascending */
    qsort(temp_moves, move_count, sizeof(FleetMove), move_output_cmp);

    if (move_count > max_moves) {
        free(temp_moves);
        free(deficit_slots);
        free(movers);
        free(released_flag);
        free(current_counts);
        return STATUS_ERR_NOMEM;
    }

    memcpy(moves_out, temp_moves, move_count * sizeof(FleetMove));
    *n_moves = move_count;

    free(temp_moves);
    free(deficit_slots);
    free(movers);
    free(released_flag);
    free(current_counts);
    return STATUS_OK;
}
