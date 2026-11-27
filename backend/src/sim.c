/**
 * @file sim.c
 * @brief Discrete-event simulation engine implementation.
 */

#include "sim.h"
#include "heap.h"
#include "queue.h"
#include "pathfind.h"
#include "fleet_alloc.h"
#include "traffic_sim.h"
#include "reroute.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

#define SIM_HIST_BUCKETS 240
#define SIM_HIST_OVERFLOW 240
#define SIM_HIST_TOTAL 241
#define SIM_MAX_LOG_STORED 2000

typedef struct SimPassenger {
    double arrival_s;
    int ride_stops;
} SimPassenger;

typedef struct {
    Queue *queue;
    SimPassenger *pending;
    size_t pending_count;
    size_t pending_cap;
    double first_event_start_min;
    double first_flag_min;
    int is_event_active;
    size_t hour_arrivals[24];
} SimStop;

typedef struct {
    int id;
    SimBusState state;
    int route;
    int stop_idx;
    int pos;
    int load;
    int capacity;
    int *alight;
    size_t alight_cap;
    uint64_t epoch;
    int pending_route;
    int pending_entry;
    double pending_deadhead_s;
    int pending_recompute;
    int arrivals_in_loop;
    double loop_start_s;
    int pax_in_loop;
    int motion_moving;
    int motion_route;
    int motion_leg_index;
    int motion_from_stop;
    int motion_to_stop;
    double motion_depart_s;
    double motion_arrive_s;
    uint64_t hops;
} SimBus;

typedef enum {
    SIM_EVT_SCENARIO = 0,
    SIM_EVT_SLICE    = 1,
    SIM_EVT_ARRIVE   = 2,
    SIM_EVT_DEPART   = 3
} SimEventKind;

typedef struct {
    double time_s;
    SimEventKind kind;
    uint64_t seq;
    int bus_id;
    uint64_t epoch;
    int arg;
    SimScenario scenario;
} SimEvent;

struct Sim {
    NetworkModel *net;
    SimConfig cfg;
    double now_s;
    int finished;
    uint64_t event_seq;

    double *prop_weights;

    SimStop *stops;
    size_t n_stops;

    SimBus *buses;
    size_t n_buses;

    Heap *event_heap;

    AnomalyDetector *detector;
    AnomalyFlag *flags;
    size_t flag_count;
    size_t flag_cap;

    double last_decision_min;

    /* Statistics */
    SimStats stats;
    SimRouteStats *route_stats;
    size_t hist[SIM_HIST_TOTAL];
    double load_sum;
    size_t departures;

    /* Logs */
    char log_lines[SIM_MAX_LOG_STORED][160];
    size_t log_count;
    size_t log_total_count;

    /* Loop observer */
    SimLoopFn loop_fn;
    void *loop_ctx;
};

/* --- Helpers --- */

static int event_cmp(const void *pa, const void *pb) {
    const SimEvent *a = (const SimEvent *)pa;
    const SimEvent *b = (const SimEvent *)pb;
    if (a->time_s != b->time_s) {
        return (a->time_s < b->time_s) ? -1 : 1;
    }
    if (a->kind != b->kind) {
        return (a->kind < b->kind) ? -1 : 1;
    }
    if (a->seq != b->seq) {
        return (a->seq < b->seq) ? -1 : 1;
    }
    return 0;
}

static void sim_log_msg(Sim *s, const char *fmt, ...) {
    if (!s) return;
    s->log_total_count++;
    if (s->log_count >= SIM_MAX_LOG_STORED) {
        return;
    }

    int sec = (int)s->now_s;
    if (sec < 0) sec = 0;
    int hh = sec / 3600;
    int mm = (sec % 3600) / 60;
    int ss = sec % 60;

    char prefix[16];
    snprintf(prefix, sizeof(prefix), "[%02d:%02d:%02d] ", hh, mm, ss);

    char body[144];
    va_list args;
    va_start(args, fmt);
    vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);

    snprintf(s->log_lines[s->log_count], sizeof(s->log_lines[s->log_count]), "%s%s", prefix, body);
    s->log_count++;
}

static Status sim_push_event(Sim *s, double time_s, SimEventKind kind, int bus_id,
                             uint64_t epoch, int arg, const SimScenario *sc) {
    SimEvent *ev = (SimEvent *)malloc(sizeof(SimEvent));
    if (!ev) return STATUS_ERR_NOMEM;

    ev->time_s = time_s;
    ev->kind = kind;
    ev->seq = s->event_seq++;
    ev->bus_id = bus_id;
    ev->epoch = epoch;
    ev->arg = arg;
    if (sc) {
        ev->scenario = *sc;
    } else {
        memset(&ev->scenario, 0, sizeof(SimScenario));
    }

    Status st = heap_push(s->event_heap, ev);
    if (st != STATUS_OK) {
        free(ev);
        return st;
    }
    return STATUS_OK;
}

static void stop_flush(Sim *s, size_t stop_idx, double now_s) {
    if (stop_idx >= s->n_stops) return;
    SimStop *st = &s->stops[stop_idx];
    size_t write_idx = 0;

    for (size_t i = 0; i < st->pending_count; i++) {
        if (st->pending[i].arrival_s <= now_s) {
            SimPassenger *p = (SimPassenger *)malloc(sizeof(SimPassenger));
            if (p) {
                *p = st->pending[i];
                queue_enqueue(st->queue, p);
            }
        } else {
            st->pending[write_idx++] = st->pending[i];
        }
    }
    st->pending_count = write_idx;
}

static void flush_all_stops(Sim *s, double now_s) {
    for (size_t i = 0; i < s->n_stops; i++) {
        stop_flush(s, i, now_s);
    }
}

static int pending_cmp(const void *pa, const void *pb) {
    const SimPassenger *a = (const SimPassenger *)pa;
    const SimPassenger *b = (const SimPassenger *)pb;
    if (a->arrival_s != b->arrival_s) {
        return (a->arrival_s < b->arrival_s) ? -1 : 1;
    }
    return 0;
}

static Status stop_add_pending(Sim *s, size_t stop_idx, const SimPassenger *pax, size_t count) {
    if (count == 0) return STATUS_OK;
    SimStop *st = &s->stops[stop_idx];

    if (st->pending_count + count > st->pending_cap) {
        size_t new_cap = (st->pending_cap == 0) ? 16 : st->pending_cap * 2;
        while (new_cap < st->pending_count + count) {
            new_cap *= 2;
        }
        SimPassenger *new_arr = (SimPassenger *)realloc(st->pending, new_cap * sizeof(SimPassenger));
        if (!new_arr) return STATUS_ERR_NOMEM;
        st->pending = new_arr;
        st->pending_cap = new_cap;
    }

    for (size_t i = 0; i < count; i++) {
        st->pending[st->pending_count++] = pax[i];
        if (s->detector) {
            anomaly_record_arrival(s->detector, stop_idx);
        }
    }

    qsort(st->pending, st->pending_count, sizeof(SimPassenger), pending_cmp);
    return STATUS_OK;
}

static void record_boarded(Sim *s, size_t route, double arrival_s, double wait_s) {
    double warmup_end_s = (s->cfg.start_min + s->cfg.warmup_min) * 60.0;
    if (arrival_s < warmup_end_s) {
        return;
    }

    s->stats.boarded++;
    s->stats.wait_sum_s += wait_s;
    if (wait_s > s->stats.wait_max_s) {
        s->stats.wait_max_s = wait_s;
    }

    int b = (int)floor(wait_s / 30.0);
    if (b < 0) b = 0;
    if (b >= SIM_HIST_BUCKETS) b = SIM_HIST_OVERFLOW;
    s->hist[b]++;

    if (route < network_route_count(s->net)) {
        s->route_stats[route].boarded++;
        s->route_stats[route].wait_sum_s += wait_s;
        if (wait_s > s->route_stats[route].wait_max_s) {
            s->route_stats[route].wait_max_s = wait_s;
        }
    }
}

static void bus_breakdown(Sim *s, int bus_id) {
    if (bus_id < 0 || (size_t)bus_id >= s->n_buses) return;
    SimBus *b = &s->buses[bus_id];
    if (b->state == SIM_BUS_BROKEN) return;

    s->stats.stranded_by_breakdown += (size_t)b->load;
    b->load = 0;
    if (b->alight && b->alight_cap > 0) {
        memset(b->alight, 0, b->alight_cap * sizeof(int));
    }
    b->pending_route = -2;
    b->pending_recompute = 0;
    b->arrivals_in_loop = 0;
    b->loop_start_s = 0.0;
    b->pax_in_loop = 0;
    b->epoch++;

    if (b->state == SIM_BUS_RUNNING) {
        b->stop_idx = (int)network_route_stop(s->net, (size_t)b->route, (size_t)b->pos);
    } else if (b->state == SIM_BUS_DEADHEAD) {
        b->stop_idx = b->pending_entry;
    }
    b->state = SIM_BUS_BROKEN;
    b->motion_moving = 0;
    b->motion_route = -1;
    b->motion_leg_index = -1;
    b->motion_from_stop = b->stop_idx;
    b->motion_to_stop = b->stop_idx;
    b->motion_depart_s = s->now_s;
    b->motion_arrive_s = s->now_s;
    sim_log_msg(s, "bus %d broken at stop %d", bus_id, b->stop_idx);
}

static Status bus_repair(Sim *s, int bus_id) {
    if (bus_id < 0 || (size_t)bus_id >= s->n_buses) return STATUS_ERR_INVALID;
    SimBus *b = &s->buses[bus_id];
    if (b->state != SIM_BUS_BROKEN) {
        return STATUS_ERR_INVALID;
    }
    b->state = SIM_BUS_RESERVE;
    b->route = -1;
    b->motion_moving = 0;
    b->motion_route = -1;
    b->motion_leg_index = -1;
    b->motion_from_stop = b->stop_idx;
    b->motion_to_stop = b->stop_idx;
    b->motion_depart_s = s->now_s;
    b->motion_arrive_s = s->now_s;
    sim_log_msg(s, "bus %d repaired at stop %d, now reserve", bus_id, b->stop_idx);
    return STATUS_OK;
}

static void process_arrive(Sim *s, SimEvent *ev);
static void process_depart(Sim *s, SimEvent *ev);

static void process_arrive(Sim *s, SimEvent *ev) {
    int bus_id = ev->bus_id;
    SimBus *b = &s->buses[bus_id];

    if (b->state == SIM_BUS_DEADHEAD) {
        b->state = SIM_BUS_RUNNING;
        b->route = b->pending_entry; /* target route is in pending_entry/b->route */
        b->route = ev->arg;          /* route was passed in arg */
        b->pos = (int)network_stop_pos(s->net, (size_t)b->stop_idx);
        size_t m = network_route_stop_count(s->net, (size_t)b->route);
        if (m > b->alight_cap) {
            int *new_a = (int *)realloc(b->alight, m * sizeof(int));
            if (new_a) {
                b->alight = new_a;
                b->alight_cap = m;
            }
        }
        if (b->alight) {
            memset(b->alight, 0, m * sizeof(int));
        }
        b->load = 0;
        b->arrivals_in_loop = 0;
        b->loop_start_s = s->now_s;
        b->pax_in_loop = 0;
    }

    if (b->state != SIM_BUS_RUNNING) {
        return;
    }

    size_t route = (size_t)b->route;
    size_t pos = (size_t)b->pos;
    size_t stop_idx = (size_t)network_route_stop(s->net, route, pos);
    b->stop_idx = (int)stop_idx;

    stop_flush(s, stop_idx, s->now_s);

    size_t m = network_route_stop_count(s->net, route);
    int alighted = (b->alight && pos < b->alight_cap) ? b->alight[pos] : 0;
    b->load -= alighted;
    if (b->alight && pos < b->alight_cap) {
        b->alight[pos] = 0;
    }

    if (b->arrivals_in_loop == 0) {
        b->arrivals_in_loop = 1;
        b->loop_start_s = s->now_s;
        b->pax_in_loop = 0;
    } else {
        b->arrivals_in_loop++;
        if (b->arrivals_in_loop == (int)m + 1) {
            if (s->loop_fn) {
                s->loop_fn(s->loop_ctx, bus_id, (int)route, b->loop_start_s, s->now_s, b->pax_in_loop);
            }
            b->arrivals_in_loop = 1;
            b->loop_start_s = s->now_s;
            b->pax_in_loop = 0;
        }
    }

    /* Pending reassignment check */
    if (b->pending_route != -2) {
        int execute_move = (s->cfg.reassign_mode == SIM_REASSIGN_DUMP) || (b->load == 0);
        if (execute_move) {
            b->arrivals_in_loop = 0;
            b->loop_start_s = 0.0;
            b->pax_in_loop = 0;
            if (s->cfg.reassign_mode == SIM_REASSIGN_DUMP) {
                s->stats.forced_dumps += (size_t)b->load;
            }
            b->load = 0;
            if (b->alight) {
                memset(b->alight, 0, b->alight_cap * sizeof(int));
            }
            if (b->pending_route == -1) {
                b->state = SIM_BUS_RESERVE;
                b->stop_idx = (int)stop_idx;
                b->route = -1;
                b->motion_moving = 0;
                b->motion_route = -1;
                b->motion_leg_index = -1;
                b->motion_from_stop = (int)stop_idx;
                b->motion_to_stop = (int)stop_idx;
                b->motion_depart_s = s->now_s;
                b->motion_arrive_s = s->now_s;
                sim_log_msg(s, "bus %d reassigned to reserve at stop %zu", bus_id, stop_idx);
            } else {
                int target_route = b->pending_route;
                int entry_stop = b->pending_entry;
                double deadhead_s = b->pending_deadhead_s;
                if (s->cfg.reassign_mode == SIM_REASSIGN_DRAIN || b->pending_recompute) {
                    int e = -1;
                    double dh = network_entry_time_s(s->net, stop_idx, (size_t)target_route, &e);
                    if (dh >= 0.0) {
                        entry_stop = e;
                        deadhead_s = dh;
                    }
                }
                b->state = SIM_BUS_DEADHEAD;
                b->stop_idx = entry_stop;
                b->pending_entry = entry_stop;
                b->motion_moving = 1;
                b->motion_route = target_route;
                b->motion_leg_index = -1;
                b->motion_from_stop = (int)stop_idx;
                b->motion_to_stop = entry_stop;
                b->motion_depart_s = s->now_s;
                b->motion_arrive_s = s->now_s + deadhead_s;
                s->stats.moves_applied++;
                s->stats.deadhead_s_total += deadhead_s;
                sim_push_event(s, s->now_s + deadhead_s, SIM_EVT_ARRIVE, bus_id, b->epoch, target_route, NULL);
                sim_log_msg(s, "bus %d deadheading to route %d entry stop %d (%.1f s)",
                            bus_id, target_route, b->stop_idx, deadhead_s);
            }
            b->pending_route = -2;
            b->pending_recompute = 0;
            return;
        }
    }

    /* Boarding */
    int nb = 0;
    if (b->pending_route == -2) {
        int space = b->capacity - b->load;
        SimStop *st = &s->stops[stop_idx];
        size_t q_sz = queue_size(st->queue);
        nb = (space > (int)q_sz) ? (int)q_sz : space;
        if (nb < 0) nb = 0;

        for (int i = 0; i < nb; i++) {
            SimPassenger *p = (SimPassenger *)queue_dequeue(st->queue);
            if (!p) break;
            double wait_s = s->now_s - p->arrival_s;
            record_boarded(s, route, p->arrival_s, wait_s);
            size_t drop_pos = (pos + (size_t)p->ride_stops) % m;
            if (b->alight && drop_pos < b->alight_cap) {
                b->alight[drop_pos]++;
            }
            b->load++;
            free(p);
        }

        b->pax_in_loop += nb;

        if (queue_size(st->queue) > 0) {
            s->stats.full_bus_events++;
        }
    }

    double dwell = s->cfg.dwell_s + s->cfg.per_pax_s * (double)(alighted + nb);
    s->load_sum += (double)b->load / (double)b->capacity;
    s->departures++;
    s->stats.bus_arrivals++;

    b->motion_moving = 0;
    b->motion_route = (int)route;
    b->motion_leg_index = -1;
    b->motion_from_stop = (int)stop_idx;
    b->motion_to_stop = (int)stop_idx;
    b->motion_depart_s = s->now_s + dwell;
    b->motion_arrive_s = s->now_s + dwell;

    sim_push_event(s, s->now_s + dwell, SIM_EVT_DEPART, bus_id, b->epoch, 0, NULL);
}

static void process_depart(Sim *s, SimEvent *ev) {
    int bus_id = ev->bus_id;
    SimBus *b = &s->buses[bus_id];
    if (b->state != SIM_BUS_RUNNING) return;

    size_t route = (size_t)b->route;
    size_t pos = (size_t)b->pos;
    size_t m = network_route_stop_count(s->net, route);

    double t_leg = network_leg_time_s(s->net, route, pos, COST_TRAFFIC);
    if (t_leg < 0.0) {
        s->stats.stalled_s += 60.0;
        b->motion_moving = 0;
        b->motion_route = (int)route;
        b->motion_leg_index = -1;
        b->motion_from_stop = b->stop_idx;
        b->motion_to_stop = b->stop_idx;
        b->motion_depart_s = s->now_s + 60.0;
        b->motion_arrive_s = s->now_s + 60.0;
        sim_push_event(s, s->now_s + 60.0, SIM_EVT_DEPART, bus_id, b->epoch, 0, NULL);
        return;
    }

    int from_stop = (int)network_route_stop(s->net, route, pos);
    double u = demand_unit(demand_hash(s->cfg.seed, (uint64_t)(5000000 + bus_id), b->hops, 31));
    b->hops++;
    double travel = t_leg * (1.0 + s->cfg.travel_noise * (2.0 * u - 1.0));
    b->pos = (int)((pos + 1) % m);
    b->stop_idx = (int)network_route_stop(s->net, route, (size_t)b->pos);

    b->motion_moving = 1;
    b->motion_route = (int)route;
    b->motion_leg_index = (int)pos;
    b->motion_from_stop = from_stop;
    b->motion_to_stop = b->stop_idx;
    b->motion_depart_s = s->now_s;
    b->motion_arrive_s = s->now_s + travel;

    sim_push_event(s, s->now_s + travel, SIM_EVT_ARRIVE, bus_id, b->epoch, b->route, NULL);
}

static void generate_arrivals_for_slice(Sim *s, double t) {
    double t_min = t / 60.0;
    int hour = (int)floor(t_min / 60.0);
    uint64_t seed = s->cfg.seed;
    int day = s->cfg.day;

    for (size_t stop_idx = 0; stop_idx < s->n_stops; stop_idx++) {
        SimStop *st = &s->stops[stop_idx];
        const DemandStop *ds = network_stop(s->net, stop_idx);
        StopType st_type = ds ? ds->type : STOP_RESIDENTIAL;
        double scale = ds ? ds->scale : 1.0;

        double rate = demand_expected(st_type, scale, hour, s->cfg.day_type, s->cfg.rain, st->is_event_active);
        double lambda = rate * s->cfg.slice_min / 60.0;
        uint64_t key = (uint64_t)stop_idx * 1440 + (uint64_t)t_min;
        int count = (int)demand_poisson_sample(lambda, demand_hash(seed, (uint64_t)day, key, 777));
        if (count <= 0) continue;

        int rt = network_stop_route(s->net, stop_idx);
        size_t route = (rt >= 0) ? (size_t)rt : 0;
        size_t m = network_route_stop_count(s->net, route);
        int max_ride = (int)m - 1;
        if (max_ride > 8) max_ride = 8;
        if (max_ride < 1) max_ride = 1;

        SimPassenger *batch = (SimPassenger *)malloc((size_t)count * sizeof(SimPassenger));
        if (!batch) continue;

        for (int k = 0; k < count; k++) {
            double u = demand_unit(demand_hash(seed, (uint64_t)day, key, (uint64_t)(10000 + k)));
            batch[k].arrival_s = t + u * s->cfg.slice_min * 60.0;
            batch[k].ride_stops = 1 + (int)(demand_hash(seed, (uint64_t)day, key, (uint64_t)(20000 + k)) % (uint64_t)max_ride);
        }

        if (stop_add_pending(s, stop_idx, batch, (size_t)count) == STATUS_OK) {
            if (hour >= 0 && hour < 24) {
                s->stops[stop_idx].hour_arrivals[hour] += (size_t)count;
            }
        }
        free(batch);
    }
}

static size_t run_reallocation_decision(Sim *s, double t_min) {
    size_t n_routes = network_route_count(s->net);
    FleetBus *snap = (FleetBus *)malloc(s->n_buses * sizeof(FleetBus));
    if (!snap) return 0;

    size_t n_snap = 0;
    for (size_t i = 0; i < s->n_buses; i++) {
        SimBus *b = &s->buses[i];
        if (b->state == SIM_BUS_RUNNING || b->state == SIM_BUS_DEADHEAD) {
            snap[n_snap].bus_id = b->id;
            if (b->pending_route >= 0) {
                snap[n_snap].route = b->pending_route;
            } else if (b->state == SIM_BUS_DEADHEAD) {
                snap[n_snap].route = b->route;
            } else {
                snap[n_snap].route = b->route;
            }

            if (b->state == SIM_BUS_RUNNING) {
                snap[n_snap].stop_idx = (int)network_route_stop(s->net, (size_t)b->route, (size_t)b->pos);
                snap[n_snap].load = b->load;
            } else {
                snap[n_snap].stop_idx = b->stop_idx;
                snap[n_snap].load = 1000000; /* huge load so released last */
            }
            n_snap++;
        }
    }

    if (n_snap < n_routes) {
        sim_log_msg(s, "reallocation decision skipped (only %zu buses active)", n_snap);
        free(snap);
        return 0;
    }

    s->stats.decisions++;

    double *demands = (double *)malloc(n_routes * sizeof(double));
    double *cycles = (double *)malloc(n_routes * sizeof(double));
    int *c_cur = (int *)calloc(n_routes, sizeof(int));
    int *targets = (int *)malloc(n_routes * sizeof(int));

    if (!demands || !cycles || !c_cur || !targets) {
        free(snap); free(demands); free(cycles); free(c_cur); free(targets);
        return 0;
    }

    int hour = (int)floor(t_min / 60.0);
    int *ev_arr = NULL;
    size_t n_ev = 0;
    if (s->cfg.predictor == NULL) {
        /* Oracle knows active events */
        ev_arr = (int *)malloc(s->n_stops * sizeof(int));
        if (ev_arr) {
            for (size_t i = 0; i < s->n_stops; i++) {
                if (s->stops[i].is_event_active) {
                    ev_arr[n_ev++] = (int)i;
                }
            }
        }
    }

    network_route_demands(s->net, s->cfg.predictor, hour, s->cfg.day_type, s->cfg.rain, ev_arr, n_ev, demands);
    network_route_cycles(s->net, cycles);
    free(ev_arr);

    for (size_t i = 0; i < n_snap; i++) {
        if (snap[i].route >= 0 && (size_t)snap[i].route < n_routes) {
            c_cur[snap[i].route]++;
        }
    }

    FleetPriority prio = (s->cfg.policy == SIM_POLICY_GREEDY_SPEC) ?
                         FLEET_PRIO_DEMAND_PER_BUS : FLEET_PRIO_MARGINAL_WAIT;
    fleet_targets_greedy(demands, cycles, n_routes, n_snap, 1, prio, targets);

    char d_buf[256];
    int d_pos = 0;
    d_pos += snprintf(d_buf + d_pos, sizeof(d_buf) - (size_t)d_pos, "[");
    for (size_t r = 0; r < n_routes; r++) {
        d_pos += snprintf(d_buf + d_pos, sizeof(d_buf) - (size_t)d_pos, "%s%.1f", (r > 0 ? ", " : ""), demands[r]);
    }
    snprintf(d_buf + d_pos, sizeof(d_buf) - (size_t)d_pos, "]");

    char t_buf[128];
    int t_pos = 0;
    t_pos += snprintf(t_buf + t_pos, sizeof(t_buf) - (size_t)t_pos, "[");
    for (size_t r = 0; r < n_routes; r++) {
        t_pos += snprintf(t_buf + t_pos, sizeof(t_buf) - (size_t)t_pos, "%s%d", (r > 0 ? ", " : ""), targets[r]);
    }
    snprintf(t_buf + t_pos, sizeof(t_buf) - (size_t)t_pos, "]");

    sim_log_msg(s, "decision %zu (hour %d): demands %s targets %s",
                s->stats.decisions, hour, d_buf, t_buf);

    double proxy_cur = fleet_wait_proxy_s(demands, cycles, c_cur, n_routes);
    double proxy_new = fleet_wait_proxy_s(demands, cycles, targets, n_routes);
    size_t applied = 0;

    if (proxy_cur > 0.0 && (proxy_cur - proxy_new) / proxy_cur < s->cfg.realloc_hysteresis) {
        sim_log_msg(s, "reallocation decision: no change (improvement below hysteresis)");
    } else {
        FleetMove *moves = (FleetMove *)malloc(n_snap * sizeof(FleetMove));
        size_t n_moves = 0;
        if (moves) {
            fleet_plan_moves(s->net, snap, n_snap, targets, n_routes, moves, n_snap, &n_moves);
            for (size_t m = 0; m < n_moves; m++) {
                int bus_id = moves[m].bus_id;
                SimBus *b = &s->buses[bus_id];
                if (b->state == SIM_BUS_RUNNING) {
                    b->pending_route = moves[m].to_route;
                    b->pending_entry = (int)moves[m].entry_stop;
                    b->pending_deadhead_s = moves[m].deadhead_s;
                    b->pending_recompute = 0;
                    applied++;
                    sim_log_msg(s, "planned move: bus %d from route %d to route %d",
                                bus_id, moves[m].from_route, moves[m].to_route);
                } else {
                    sim_log_msg(s, "skipped move for bus %d (not running)", bus_id);
                }
            }
            if (applied > 0) {
                s->stats.decisions_applied++;
            }
            free(moves);
        }
    }

    free(snap);
    free(demands);
    free(cycles);
    free(c_cur);
    free(targets);
    return applied;
}

static void emergency_dispatch(Sim *s, size_t flagged_stop) {
    int rt = network_stop_route(s->net, flagged_stop);
    if (rt < 0) return;
    size_t target_route = (size_t)rt;
    FleetBus *cands = (FleetBus *)malloc(s->n_buses * sizeof(FleetBus));
    if (!cands) return;

    size_t n_cands = 0;
    for (size_t i = 0; i < s->n_buses; i++) {
        if (s->buses[i].state == SIM_BUS_RESERVE) {
            cands[n_cands].bus_id = s->buses[i].id;
            cands[n_cands].route = -1;
            cands[n_cands].stop_idx = s->buses[i].stop_idx;
            cands[n_cands].load = 0;
            n_cands++;
        }
    }

    size_t cand_idx = 0;
    double deadhead_s = 0.0;
    int entry_stop = 0;

    Status st = fleet_pick_nearest(s->net, cands, n_cands, target_route,
                                   &cand_idx, &deadhead_s, &entry_stop);
    if (st == STATUS_ERR_EMPTY || n_cands == 0) {
        sim_log_msg(s, "no reserve bus for stop %zu", flagged_stop);
        free(cands);
        return;
    }

    int chosen_bus_id = cands[cand_idx].bus_id;
    free(cands);

    SimBus *b = &s->buses[chosen_bus_id];
    int from_stop = b->stop_idx;
    b->state = SIM_BUS_DEADHEAD;
    b->route = (int)target_route;
    b->stop_idx = entry_stop;
    b->pending_entry = entry_stop;
    b->motion_moving = 1;
    b->motion_route = (int)target_route;
    b->motion_leg_index = -1;
    b->motion_from_stop = from_stop;
    b->motion_to_stop = entry_stop;
    b->motion_depart_s = s->now_s;
    b->motion_arrive_s = s->now_s + deadhead_s;

    s->stats.emergency_dispatches++;
    s->stats.moves_applied++;
    s->stats.deadhead_s_total += deadhead_s;

    sim_push_event(s, s->now_s + deadhead_s, SIM_EVT_ARRIVE, chosen_bus_id, b->epoch, (int)target_route, NULL);
    sim_log_msg(s, "emergency dispatch: bus %d to route %zu entry stop %d (deadhead %.1f s)",
                chosen_bus_id, target_route, entry_stop, deadhead_s);
}

static void process_slice(Sim *s, double t) {
    double t_min = t / 60.0;

    /* (1) Flush all stops */
    flush_all_stops(s, t);

    if (t_min >= s->cfg.end_min) {
        s->finished = 1;
        return;
    }

    /* (2) Anomaly detection window closure */
    if (t_min > s->cfg.start_min && s->detector) {
        double finished_slice_min = t_min - s->cfg.slice_min;
        int hour_finished = (int)floor(finished_slice_min / 60.0);
        double *expected = (double *)malloc(s->n_stops * sizeof(double));
        AnomalyFlag *flags = (AnomalyFlag *)malloc(s->n_stops * sizeof(AnomalyFlag));

        if (expected && flags) {
            for (size_t i = 0; i < s->n_stops; i++) {
                const DemandStop *ds = network_stop(s->net, i);
                StopType type = ds ? ds->type : STOP_RESIDENTIAL;
                double scale = ds ? ds->scale : 1.0;
                double rate = 0.0;
                if (s->cfg.predictor) {
                    int node = (int)(ds ? ds->node : 0);
                    rate = dp_predict(s->cfg.predictor, node, type, scale,
                                      hour_finished, s->cfg.day_type, s->cfg.rain, 0);
                } else {
                    rate = demand_expected(type, scale, hour_finished, s->cfg.day_type, s->cfg.rain, 0);
                }
                expected[i] = rate * s->cfg.slice_min / 60.0;
            }

            size_t n_fl = 0;
            anomaly_close_window(s->detector, t_min, expected, flags, s->n_stops, &n_fl);

            for (size_t f = 0; f < n_fl; f++) {
                s->stats.anomaly_flags++;
                size_t st_idx = flags[f].stop;
                if (s->stops[st_idx].first_flag_min < 0.0) {
                    s->stops[st_idx].first_flag_min = t_min;
                }

                if (s->flag_count == s->flag_cap) {
                    size_t nc = (s->flag_cap == 0) ? 16 : s->flag_cap * 2;
                    AnomalyFlag *nf = (AnomalyFlag *)realloc(s->flags, nc * sizeof(AnomalyFlag));
                    if (nf) {
                        s->flags = nf;
                        s->flag_cap = nc;
                    }
                }
                if (s->flag_count < s->flag_cap) {
                    s->flags[s->flag_count++] = flags[f];
                }

                sim_log_msg(s, "anomaly detected at stop %zu (obs %d, exp %.1f, z %.2f)",
                            flags[f].stop, flags[f].observed, flags[f].expected, flags[f].z_score);

                if (s->cfg.anomaly_dispatch) {
                    emergency_dispatch(s, flags[f].stop);
                }
            }
        }
        free(expected);
        free(flags);
    }

    /* (3) Dynamic reallocation decision */
    if (t_min > s->cfg.start_min &&
        (s->cfg.policy == SIM_POLICY_GREEDY_SPEC || s->cfg.policy == SIM_POLICY_GREEDY_MARGINAL)) {
        if ((t_min - s->last_decision_min) >= s->cfg.realloc_interval_min - 1e-6) {
            run_reallocation_decision(s, t_min);
            s->last_decision_min = t_min;
        }
    }

    /* (4) Traffic simulation apply */
    if (s->cfg.apply_traffic) {
        int minute_of_day = (int)floor(t / 60.0);
        traffic_sim_apply(network_graph(s->net), minute_of_day, s->cfg.rain);
    }

    /* (5) Generate arrivals */
    if (s->cfg.arrivals_enabled) {
        generate_arrivals_for_slice(s, t);
    }

    /* (6) Schedule next slice */
    double next_t = t + s->cfg.slice_min * 60.0;
    if (next_t <= s->cfg.end_min * 60.0 + 1e-6) {
        sim_push_event(s, next_t, SIM_EVT_SLICE, -1, 0, 0, NULL);
    }
}

static void process_scenario(Sim *s, SimEvent *ev) {
    const SimScenario *sc = &ev->scenario;
    switch (sc->kind) {
        case SIM_SCEN_RAIN_ON:
            s->cfg.rain = 1;
            sim_log_msg(s, "scenario: rain turned ON");
            break;
        case SIM_SCEN_RAIN_OFF:
            s->cfg.rain = 0;
            sim_log_msg(s, "scenario: rain turned OFF");
            break;
        case SIM_SCEN_EVENT_ON:
            if (sc->arg >= 0 && (size_t)sc->arg < s->n_stops) {
                s->stops[sc->arg].is_event_active = 1;
                if (s->stops[sc->arg].first_event_start_min < 0.0) {
                    s->stops[sc->arg].first_event_start_min = sc->time_min;
                }
                sim_log_msg(s, "scenario: event ON at stop %d", sc->arg);
            }
            break;
        case SIM_SCEN_EVENT_OFF:
            if (sc->arg >= 0 && (size_t)sc->arg < s->n_stops) {
                s->stops[sc->arg].is_event_active = 0;
                sim_log_msg(s, "scenario: event OFF at stop %d", sc->arg);
            }
            break;
        case SIM_SCEN_BLOCK_EDGE: {
            RouteManager *rm = network_route_manager(s->net);
            if (rm) {
                rm_block_edge(rm, sc->arg, NULL);
                sim_log_msg(s, "scenario: blocked edge %d", sc->arg);
            }
            break;
        }
        case SIM_SCEN_UNBLOCK_EDGE: {
            RouteManager *rm = network_route_manager(s->net);
            if (rm) {
                rm_unblock_edge(rm, sc->arg, NULL);
                sim_log_msg(s, "scenario: unblocked edge %d", sc->arg);
            }
            break;
        }
        case SIM_SCEN_BREAKDOWN:
            bus_breakdown(s, sc->arg);
            break;
        case SIM_SCEN_REPAIR:
            bus_repair(s, sc->arg);
            break;
    }
}

/* --- Public API Implementation --- */

void sim_config_default(SimConfig *c) {
    if (!c) return;
    c->seed = 42;
    c->day = 0;
    c->day_type = DAY_WEEKDAY;
    c->rain = 0;
    c->start_min = 360.0;
    c->end_min = 1320.0;
    c->warmup_min = 30.0;
    c->slice_min = 15.0;
    c->arrivals_enabled = 1;
    c->dwell_s = NETWORK_DEFAULT_DWELL_S;
    c->per_pax_s = 1.0;
    c->bus_capacity = 60;
    c->travel_noise = 0.10;
    c->n_buses = 36;
    c->n_reserve = 0;
    c->depot_stop = 0;
    c->policy = SIM_POLICY_FIXED_EQUAL;
    c->prop_weights = NULL;
    c->predictor = NULL;
    c->realloc_interval_min = 30.0;
    c->realloc_hysteresis = 0.02;
    c->apply_traffic = 1;
    c->anomaly_detect = 0;
    c->anomaly_dispatch = 0;
    c->anomaly = anomaly_config_default();
    c->initial_spacing = SIM_SPACING_BY_STOP;
    c->reassign_mode = SIM_REASSIGN_DUMP;
}

Status sim_create(NetworkModel *net, const SimConfig *cfg, Sim **out) {
    if (out) *out = NULL;
    if (!net || !out) return STATUS_ERR_NULL;

    SimConfig c;
    if (cfg) {
        c = *cfg;
    } else {
        sim_config_default(&c);
    }

    /* Validations */
    if (c.start_min < 0.0 || c.end_min > 1440.0 || c.start_min >= c.end_min) {
        return STATUS_ERR_INVALID;
    }
    int valid_slice = (fabs(c.slice_min - 5.0) < 1e-6 ||
                       fabs(c.slice_min - 10.0) < 1e-6 ||
                       fabs(c.slice_min - 15.0) < 1e-6 ||
                       fabs(c.slice_min - 20.0) < 1e-6 ||
                       fabs(c.slice_min - 30.0) < 1e-6 ||
                       fabs(c.slice_min - 60.0) < 1e-6);
    if (!valid_slice) return STATUS_ERR_INVALID;
    if (fmod(c.start_min, c.slice_min) > 1e-6 || fmod(c.end_min, c.slice_min) > 1e-6) {
        return STATUS_ERR_INVALID;
    }
    if (c.warmup_min < 0.0 || c.warmup_min >= (c.end_min - c.start_min)) {
        return STATUS_ERR_INVALID;
    }
    if (c.dwell_s < 0.0 || c.per_pax_s < 0.0) return STATUS_ERR_INVALID;
    if (c.bus_capacity < 1) return STATUS_ERR_INVALID;
    if (c.travel_noise < 0.0 || c.travel_noise >= 0.5) return STATUS_ERR_INVALID;

    size_t n_routes = network_route_count(net);
    if (c.n_reserve < 0 || (c.n_buses - c.n_reserve) < (int)n_routes) {
        return STATUS_ERR_INVALID;
    }
    size_t n_stops = network_stop_count(net);
    if (c.depot_stop < 0 || (size_t)c.depot_stop >= n_stops) {
        return STATUS_ERR_INVALID;
    }
    if (c.policy == SIM_POLICY_FIXED_PROPORTIONAL && c.prop_weights == NULL) {
        return STATUS_ERR_INVALID;
    }
    if (c.realloc_interval_min < c.slice_min || fmod(c.realloc_interval_min, c.slice_min) > 1e-6) {
        return STATUS_ERR_INVALID;
    }
    if (c.realloc_hysteresis < 0.0 || c.realloc_hysteresis >= 1.0) {
        return STATUS_ERR_INVALID;
    }
    if (c.anomaly_dispatch && !c.anomaly_detect) {
        return STATUS_ERR_INVALID;
    }
    if (c.initial_spacing != SIM_SPACING_BY_STOP && c.initial_spacing != SIM_SPACING_BY_TIME) {
        return STATUS_ERR_INVALID;
    }
    if (c.reassign_mode != SIM_REASSIGN_DUMP && c.reassign_mode != SIM_REASSIGN_DRAIN) {
        return STATUS_ERR_INVALID;
    }

    int needs_matrix = (c.policy == SIM_POLICY_GREEDY_SPEC ||
                        c.policy == SIM_POLICY_GREEDY_MARGINAL ||
                        c.anomaly_dispatch);
    if (needs_matrix && n_stops >= 2) {
        if (network_stop_time_s(net, 0, 1) < 0.0) {
            return STATUS_ERR_INVALID;
        }
    }

    Sim *s = (Sim *)calloc(1, sizeof(Sim));
    if (!s) return STATUS_ERR_NOMEM;

    s->net = net;
    s->cfg = c;
    s->now_s = c.start_min * 60.0;
    s->finished = 0;
    s->event_seq = 0;
    s->last_decision_min = c.start_min;

    if (c.prop_weights && n_routes > 0) {
        s->prop_weights = (double *)malloc(n_routes * sizeof(double));
        if (!s->prop_weights) {
            sim_destroy(s);
            return STATUS_ERR_NOMEM;
        }
        memcpy(s->prop_weights, c.prop_weights, n_routes * sizeof(double));
    }

    s->event_heap = heap_create(event_cmp, free, NULL);
    if (!s->event_heap) {
        sim_destroy(s);
        return STATUS_ERR_NOMEM;
    }

    s->n_stops = n_stops;
    s->stops = (SimStop *)calloc(n_stops, sizeof(SimStop));
    if (!s->stops) {
        sim_destroy(s);
        return STATUS_ERR_NOMEM;
    }
    for (size_t i = 0; i < n_stops; i++) {
        s->stops[i].queue = queue_create(free);
        s->stops[i].first_event_start_min = -1.0;
        s->stops[i].first_flag_min = -1.0;
        if (!s->stops[i].queue) {
            sim_destroy(s);
            return STATUS_ERR_NOMEM;
        }
    }

    if (c.anomaly_detect) {
        s->detector = anomaly_create(n_stops, &c.anomaly);
        if (!s->detector) {
            sim_destroy(s);
            return STATUS_ERR_NOMEM;
        }
    }

    s->route_stats = (SimRouteStats *)calloc(n_routes, sizeof(SimRouteStats));
    if (!s->route_stats) {
        sim_destroy(s);
        return STATUS_ERR_NOMEM;
    }

    s->n_buses = (size_t)c.n_buses;
    s->buses = (SimBus *)calloc(s->n_buses, sizeof(SimBus));
    if (!s->buses) {
        sim_destroy(s);
        return STATUS_ERR_NOMEM;
    }

    for (size_t i = 0; i < s->n_buses; i++) {
        s->buses[i].id = (int)i;
        s->buses[i].capacity = c.bus_capacity;
        s->buses[i].pending_route = -2;
    }

    /* Initial fleet allocation */
    size_t active = (size_t)(c.n_buses - c.n_reserve);
    int *targets = (int *)calloc(n_routes, sizeof(int));
    if (!targets) {
        sim_destroy(s);
        return STATUS_ERR_NOMEM;
    }

    if (c.policy == SIM_POLICY_FIXED_EQUAL) {
        fleet_targets_equal(n_routes, active, targets);
    } else if (c.policy == SIM_POLICY_FIXED_PROPORTIONAL) {
        fleet_targets_proportional(s->prop_weights, n_routes, active, 1, targets);
    } else {
        int hour = (int)floor(c.start_min / 60.0);
        double *demands = (double *)malloc(n_routes * sizeof(double));
        double *cycles = (double *)malloc(n_routes * sizeof(double));
        if (demands && cycles) {
            network_route_demands(net, c.predictor, hour, c.day_type, c.rain, NULL, 0, demands);
            network_route_cycles(net, cycles);
            FleetPriority prio = (c.policy == SIM_POLICY_GREEDY_SPEC) ?
                                 FLEET_PRIO_DEMAND_PER_BUS : FLEET_PRIO_MARGINAL_WAIT;
            fleet_targets_greedy(demands, cycles, n_routes, active, 1, prio, targets);
        }
        free(demands);
        free(cycles);
    }

    size_t bus_idx = 0;
    for (size_t r = 0; r < n_routes; r++) {
        int c_r = targets[r];
        size_t m = network_route_stop_count(net, r);
        if (c_r <= 0 || m == 0) continue;

        if (c.initial_spacing == SIM_SPACING_BY_TIME) {
            double *T = (double *)calloc(m + 1, sizeof(double));
            int fallback = (!T);
            if (T) {
                T[0] = 0.0;
                for (size_t k = 0; k < m; k++) {
                    double t_leg = network_leg_time_s(net, r, k, COST_STATIC);
                    if (t_leg < 0.0) {
                        fallback = 1;
                        sim_log_msg(s, "warning: unreachable leg on route %zu, falling back to stop spacing", r);
                        break;
                    }
                    T[k + 1] = T[k] + t_leg + c.dwell_s;
                }
                if (T[m] <= 0.0) fallback = 1;
            }

            if (fallback) {
                for (int j = 0; j < c_r && bus_idx < active; j++) {
                    SimBus *b = &s->buses[bus_idx];
                    b->state = SIM_BUS_RUNNING;
                    b->route = (int)r;
                    b->pos = (int)((j * (int)m) / c_r);
                    b->stop_idx = (int)network_route_stop(net, r, (size_t)b->pos);
                    b->alight_cap = m;
                    b->alight = (int *)calloc(m, sizeof(int));
                    b->motion_moving = 0;
                    b->motion_route = (int)r;
                    b->motion_leg_index = -1;
                    b->motion_from_stop = b->stop_idx;
                    b->motion_to_stop = b->stop_idx;
                    b->motion_depart_s = s->now_s;
                    b->motion_arrive_s = s->now_s;

                    sim_push_event(s, s->now_s, SIM_EVT_ARRIVE, (int)bus_idx, 0, (int)r, NULL);
                    bus_idx++;
                }
            } else {
                double cycle = T[m];
                for (int j = 0; j < c_r && bus_idx < active; j++) {
                    SimBus *b = &s->buses[bus_idx];
                    b->state = SIM_BUS_RUNNING;
                    b->route = (int)r;
                    double psi_j = (double)j * cycle / (double)c_r;
                    size_t k_star = 0;
                    for (size_t k = 0; k <= m; k++) {
                        if (T[k] >= psi_j) {
                            k_star = k;
                            break;
                        }
                    }
                    b->pos = (int)(k_star % m);
                    b->stop_idx = (int)network_route_stop(net, r, (size_t)b->pos);
                    b->alight_cap = m;
                    b->alight = (int *)calloc(m, sizeof(int));

                    double delay = T[k_star] - psi_j;
                    b->motion_moving = 0;
                    b->motion_route = (int)r;
                    b->motion_leg_index = -1;
                    b->motion_from_stop = b->stop_idx;
                    b->motion_to_stop = b->stop_idx;
                    b->motion_depart_s = s->now_s + delay;
                    b->motion_arrive_s = s->now_s + delay;
                    sim_push_event(s, s->now_s + delay, SIM_EVT_ARRIVE, (int)bus_idx, 0, (int)r, NULL);
                    bus_idx++;
                }
            }
            free(T);
        } else {
            for (int j = 0; j < c_r && bus_idx < active; j++) {
                SimBus *b = &s->buses[bus_idx];
                b->state = SIM_BUS_RUNNING;
                b->route = (int)r;
                b->pos = (int)((j * (int)m) / c_r);
                b->stop_idx = (int)network_route_stop(net, r, (size_t)b->pos);
                b->alight_cap = m;
                b->alight = (int *)calloc(m, sizeof(int));
                b->motion_moving = 0;
                b->motion_route = (int)r;
                b->motion_leg_index = -1;
                b->motion_from_stop = b->stop_idx;
                b->motion_to_stop = b->stop_idx;
                b->motion_depart_s = s->now_s;
                b->motion_arrive_s = s->now_s;

                sim_push_event(s, s->now_s, SIM_EVT_ARRIVE, (int)bus_idx, 0, (int)r, NULL);
                bus_idx++;
            }
        }
    }
    free(targets);

    for (size_t i = active; i < s->n_buses; i++) {
        s->buses[i].state = SIM_BUS_RESERVE;
        s->buses[i].route = -1;
        s->buses[i].stop_idx = c.depot_stop;
        s->buses[i].motion_moving = 0;
        s->buses[i].motion_route = -1;
        s->buses[i].motion_leg_index = -1;
        s->buses[i].motion_from_stop = c.depot_stop;
        s->buses[i].motion_to_stop = c.depot_stop;
        s->buses[i].motion_depart_s = s->now_s;
        s->buses[i].motion_arrive_s = s->now_s;
    }

    /* Schedule initial slice event at start_min */
    sim_push_event(s, s->now_s, SIM_EVT_SLICE, -1, 0, 0, NULL);

    sim_log_msg(s, "sim created: %zu routes, %zu stops, %zu buses (%d reserve)",
                n_routes, n_stops, s->n_buses, c.n_reserve);

    *out = s;
    return STATUS_OK;
}

void sim_destroy(Sim *s) {
    if (!s) return;

    if (s->event_heap) {
        heap_destroy(s->event_heap);
    }

    if (s->stops) {
        for (size_t i = 0; i < s->n_stops; i++) {
            if (s->stops[i].queue) {
                queue_destroy(s->stops[i].queue);
            }
            free(s->stops[i].pending);
        }
        free(s->stops);
    }

    if (s->buses) {
        for (size_t i = 0; i < s->n_buses; i++) {
            free(s->buses[i].alight);
        }
        free(s->buses);
    }

    if (s->detector) {
        anomaly_destroy(s->detector);
    }

    free(s->flags);
    free(s->prop_weights);
    free(s->route_stats);
    free(s);
}

Status sim_schedule(Sim *s, const SimScenario *sc) {
    if (!s || !sc) return STATUS_ERR_NULL;
    if (sc->time_min < sim_now_min(s) || sc->time_min >= s->cfg.end_min) {
        return STATUS_ERR_INVALID;
    }
    if (fmod(sc->time_min, s->cfg.slice_min) > 1e-6) {
        return STATUS_ERR_INVALID;
    }

    switch (sc->kind) {
        case SIM_SCEN_EVENT_ON:
        case SIM_SCEN_EVENT_OFF:
            if (sc->arg < 0 || (size_t)sc->arg >= s->n_stops) return STATUS_ERR_INVALID;
            break;
        case SIM_SCEN_BLOCK_EDGE:
        case SIM_SCEN_UNBLOCK_EDGE: {
            Graph *g = network_graph(s->net);
            if (sc->arg < 0 || (size_t)sc->arg >= graph_edge_count(g)) return STATUS_ERR_INVALID;
            break;
        }
        case SIM_SCEN_BREAKDOWN:
        case SIM_SCEN_REPAIR:
            if (sc->arg < 0 || (size_t)sc->arg >= s->n_buses) return STATUS_ERR_INVALID;
            break;
        default:
            break;
    }

    return sim_push_event(s, sc->time_min * 60.0, SIM_EVT_SCENARIO, -1, 0, 0, sc);
}

Status sim_inject_arrivals(Sim *s, size_t stop, double time_s, int count, int ride_stops) {
    if (!s) return STATUS_ERR_NULL;
    if (stop >= s->n_stops || count < 1) return STATUS_ERR_INVALID;
    if (time_s < s->now_s || time_s > s->cfg.end_min * 60.0) return STATUS_ERR_INVALID;

    double t_cur = (s->now_s > s->cfg.start_min * 60.0) ? s->now_s : s->cfg.start_min * 60.0;
    double slice_s = s->cfg.slice_min * 60.0;
    double slice_idx = floor((t_cur - s->cfg.start_min * 60.0) / slice_s);
    double cur_slice_end_s = s->cfg.start_min * 60.0 + (slice_idx + 1.0) * slice_s;
    if (time_s >= cur_slice_end_s) {
        return STATUS_ERR_INVALID;
    }

    int rt = network_stop_route(s->net, stop);
    if (rt < 0) return STATUS_ERR_INVALID;
    size_t route = (size_t)rt;
    size_t m = network_route_stop_count(s->net, route);
    int max_ride = (int)m - 1;
    if (max_ride > 8) max_ride = 8;
    if (max_ride < 1) max_ride = 1;

    if (ride_stops != 0) {
        if (ride_stops < 1 || (size_t)ride_stops > (m - 1)) {
            return STATUS_ERR_INVALID;
        }
    }

    SimPassenger *batch = (SimPassenger *)malloc((size_t)count * sizeof(SimPassenger));
    if (!batch) return STATUS_ERR_NOMEM;

    for (int i = 0; i < count; i++) {
        batch[i].arrival_s = time_s;
        if (ride_stops != 0) {
            batch[i].ride_stops = ride_stops;
        } else {
            uint64_t key = (uint64_t)stop * 1440 + (uint64_t)(time_s / 60.0);
            batch[i].ride_stops = 1 + (int)(demand_hash(s->cfg.seed, (uint64_t)s->cfg.day, key, (uint64_t)(20000 + i)) % (uint64_t)max_ride);
        }
    }

    Status st = stop_add_pending(s, stop, batch, (size_t)count);
    if (st == STATUS_OK) {
        int h = (int)floor(time_s / 3600.0);
        if (h >= 0 && h < 24) {
            s->stops[stop].hour_arrivals[h] += (size_t)count;
        }
    }
    free(batch);
    return st;
}

Status sim_run_until(Sim *s, double t_min) {
    if (!s) return STATUS_ERR_NULL;
    if (t_min < sim_now_min(s) - 1e-9 || t_min > s->cfg.end_min + 1e-9) {
        return STATUS_ERR_INVALID;
    }

    double target_s = t_min * 60.0;

    while (heap_size(s->event_heap) > 0) {
        SimEvent *peek_ev = (SimEvent *)heap_peek(s->event_heap);
        if (!peek_ev || peek_ev->time_s > target_s) {
            break;
        }

        SimEvent *ev = (SimEvent *)heap_pop(s->event_heap);
        if (!ev) break;

        s->now_s = ev->time_s;

        /* Epoch check for bus-specific events */
        if (ev->bus_id >= 0) {
            SimBus *b = &s->buses[ev->bus_id];
            if (ev->epoch != b->epoch) {
                free(ev);
                continue;
            }
        }

        switch (ev->kind) {
            case SIM_EVT_SCENARIO:
                process_scenario(s, ev);
                break;
            case SIM_EVT_SLICE:
                process_slice(s, ev->time_s);
                break;
            case SIM_EVT_ARRIVE:
                process_arrive(s, ev);
                break;
            case SIM_EVT_DEPART:
                process_depart(s, ev);
                break;
        }
        free(ev);
    }

    if (s->now_s < target_s) {
        s->now_s = target_s;
    }

    return STATUS_OK;
}

Status sim_run_to_end(Sim *s) {
    if (!s) return STATUS_ERR_NULL;
    return sim_run_until(s, s->cfg.end_min);
}

double sim_now_min(const Sim *s) {
    return s ? (s->now_s / 60.0) : -1.0;
}

int sim_finished(const Sim *s) {
    return s ? s->finished : 0;
}

size_t sim_bus_count(const Sim *s) {
    return s ? s->n_buses : 0;
}

Status sim_bus_info(const Sim *s, size_t bus, SimBusInfo *out) {
    if (!s || !out) return STATUS_ERR_NULL;
    if (bus >= s->n_buses) return STATUS_ERR_INVALID;

    const SimBus *b = &s->buses[bus];
    out->id = b->id;
    out->state = b->state;
    if (b->state == SIM_BUS_RUNNING) {
        out->route = b->route;
        out->stop_idx = (int)network_route_stop(s->net, (size_t)b->route, (size_t)b->pos);
    } else if (b->state == SIM_BUS_DEADHEAD) {
        out->route = b->route;
        out->stop_idx = b->stop_idx;
    } else {
        out->route = -1;
        out->stop_idx = b->stop_idx;
    }
    out->load = b->load;
    out->pending_route = b->pending_route;
    return STATUS_OK;
}

size_t sim_route_bus_count(const Sim *s, size_t r) {
    if (!s || r >= network_route_count(s->net)) return 0;
    size_t cnt = 0;
    for (size_t i = 0; i < s->n_buses; i++) {
        if ((s->buses[i].state == SIM_BUS_RUNNING || s->buses[i].state == SIM_BUS_DEADHEAD) &&
            s->buses[i].route == (int)r) {
            cnt++;
        }
    }
    return cnt;
}

long sim_queue_length(Sim *s, size_t stop) {
    if (!s || stop >= s->n_stops) return -1;
    stop_flush(s, stop, s->now_s);
    return (long)queue_size(s->stops[stop].queue);
}

Status sim_stats(Sim *s, SimStats *out) {
    if (!s || !out) return STATUS_ERR_NULL;

    flush_all_stops(s, s->now_s);
    double warmup_end_s = (s->cfg.start_min + s->cfg.warmup_min) * 60.0;

    size_t still_waiting = 0;
    double censored_sum = 0.0;

    for (size_t i = 0; i < s->n_stops; i++) {
        Queue *q = s->stops[i].queue;
        size_t sz = queue_size(q);
        for (size_t k = 0; k < sz; k++) {
            SimPassenger *p = (SimPassenger *)queue_dequeue(q);
            if (p) {
                if (p->arrival_s >= warmup_end_s) {
                    still_waiting++;
                    censored_sum += (s->now_s - p->arrival_s);
                }
                queue_enqueue(q, p);
            }
        }
    }

    SimStats st = s->stats;
    st.still_waiting = still_waiting;
    st.arrived = st.boarded + still_waiting;

    st.mean_wait_boarded_s = (st.boarded > 0) ? (st.wait_sum_s / (double)st.boarded) : 0.0;
    st.mean_wait_incl_censored_s = (st.arrived > 0) ? ((st.wait_sum_s + censored_sum) / (double)st.arrived) : 0.0;

    if (st.boarded == 0) {
        st.p50_s = -1.0;
        st.p95_s = -1.0;
    } else {
        size_t t50 = (size_t)ceil(0.50 * (double)st.boarded);
        size_t t95 = (size_t)ceil(0.95 * (double)st.boarded);
        size_t cum = 0;
        st.p50_s = -1.0;
        st.p95_s = -1.0;

        for (int b = 0; b < SIM_HIST_BUCKETS; b++) {
            cum += s->hist[b];
            if (st.p50_s < 0.0 && cum >= t50) {
                st.p50_s = (double)(b + 1) * 30.0;
            }
            if (st.p95_s < 0.0 && cum >= t95) {
                st.p95_s = (double)(b + 1) * 30.0;
            }
        }
        if (st.p50_s < 0.0) st.p50_s = 7200.0;
        if (st.p95_s < 0.0) st.p95_s = 7200.0;
    }

    st.mean_load_factor = (s->departures > 0) ? (s->load_sum / (double)s->departures) : 0.0;

    *out = st;
    return STATUS_OK;
}

Status sim_route_stats(Sim *s, size_t r, SimRouteStats *out) {
    if (!s || !out) return STATUS_ERR_NULL;
    if (r >= network_route_count(s->net)) return STATUS_ERR_INVALID;

    flush_all_stops(s, s->now_s);
    double warmup_end_s = (s->cfg.start_min + s->cfg.warmup_min) * 60.0;

    size_t still_waiting = 0;
    size_t stop_count = network_route_stop_count(s->net, r);

    for (size_t pos = 0; pos < stop_count; pos++) {
        int st_id = network_route_stop(s->net, r, pos);
        size_t stop_idx = (st_id >= 0) ? (size_t)st_id : 0;
        Queue *q = s->stops[stop_idx].queue;
        size_t sz = queue_size(q);
        for (size_t k = 0; k < sz; k++) {
            SimPassenger *p = (SimPassenger *)queue_dequeue(q);
            if (p) {
                if (p->arrival_s >= warmup_end_s) {
                    still_waiting++;
                }
                queue_enqueue(q, p);
            }
        }
    }

    SimRouteStats rst = s->route_stats[r];
    rst.still_waiting = still_waiting;
    rst.mean_wait_s = (rst.boarded > 0) ? (rst.wait_sum_s / (double)rst.boarded) : 0.0;

    *out = rst;
    return STATUS_OK;
}

size_t sim_flag_count(const Sim *s) {
    return s ? s->flag_count : 0;
}

Status sim_flag(const Sim *s, size_t i, AnomalyFlag *out) {
    if (!s || !out) return STATUS_ERR_NULL;
    if (i >= s->flag_count) return STATUS_ERR_INVALID;
    *out = s->flags[i];
    return STATUS_OK;
}

double sim_first_flag_min(const Sim *s, size_t stop) {
    if (!s || stop >= s->n_stops) return -1.0;
    return s->stops[stop].first_flag_min;
}

double sim_event_start_min(const Sim *s, size_t stop) {
    if (!s || stop >= s->n_stops) return -1.0;
    return s->stops[stop].first_event_start_min;
}

size_t sim_log_count(const Sim *s) {
    return s ? s->log_total_count : 0;
}

const char *sim_log_line(const Sim *s, size_t i) {
    if (!s || i >= s->log_count) return NULL;
    return s->log_lines[i];
}

Status sim_request_move(Sim *s, int bus_id, int to_route) {
    if (!s) return STATUS_ERR_NULL;
    if (bus_id < 0 || (size_t)bus_id >= s->n_buses) return STATUS_ERR_INVALID;

    SimBus *b = &s->buses[bus_id];
    if (b->state != SIM_BUS_RUNNING || b->pending_route != -2) {
        return STATUS_ERR_INVALID;
    }

    size_t n_routes = network_route_count(s->net);
    if (to_route < -1 || to_route >= (int)n_routes) {
        return STATUS_ERR_INVALID;
    }
    if (to_route == b->route) {
        return STATUS_ERR_INVALID;
    }
    if (to_route >= 0 && s->n_stops >= 2 && network_stop_time_s(s->net, 0, 1) < 0.0) {
        return STATUS_ERR_INVALID;
    }

    b->pending_route = to_route;
    b->pending_entry = -1;
    b->pending_deadhead_s = 0.0;
    b->pending_recompute = 1;
    sim_log_msg(s, "move requested: bus %d from route %d to route %d", bus_id, b->route, to_route);
    return STATUS_OK;
}

Status sim_set_policy(Sim *s, SimPolicy policy, const DemandPredictor *predictor) {
    if (!s) return STATUS_ERR_NULL;
    if (policy != SIM_POLICY_FIXED_EQUAL &&
        policy != SIM_POLICY_FIXED_PROPORTIONAL &&
        policy != SIM_POLICY_GREEDY_SPEC &&
        policy != SIM_POLICY_GREEDY_MARGINAL) {
        return STATUS_ERR_INVALID;
    }
    if ((policy == SIM_POLICY_GREEDY_SPEC || policy == SIM_POLICY_GREEDY_MARGINAL) &&
        s->n_stops >= 2 && network_stop_time_s(s->net, 0, 1) < 0.0) {
        return STATUS_ERR_INVALID;
    }

    s->cfg.policy = policy;
    s->cfg.predictor = predictor;
    s->last_decision_min = s->now_s / 60.0;
    sim_log_msg(s, "policy set to %d", (int)policy);
    return STATUS_OK;
}

Status sim_force_decision(Sim *s, size_t *moves_applied) {
    if (!s) return STATUS_ERR_NULL;
    if (s->cfg.policy != SIM_POLICY_GREEDY_SPEC &&
        s->cfg.policy != SIM_POLICY_GREEDY_MARGINAL) {
        return STATUS_ERR_INVALID;
    }

    double t_min = s->now_s / 60.0;
    size_t applied = run_reallocation_decision(s, t_min);
    s->last_decision_min = t_min;
    if (moves_applied) {
        *moves_applied = applied;
    }
    return STATUS_OK;
}

int sim_rain_active(const Sim *s) {
    if (!s) return 0;
    return s->cfg.rain ? 1 : 0;
}

int sim_event_active(const Sim *s, size_t stop) {
    if (!s || stop >= s->n_stops) return 0;
    return s->stops[stop].is_event_active ? 1 : 0;
}

SimPolicy sim_policy(const Sim *s) {
    if (!s) return SIM_POLICY_FIXED_EQUAL;
    return s->cfg.policy;
}

size_t sim_stop_hour_arrivals(const Sim *s, size_t stop, int hour) {
    if (!s || stop >= s->n_stops || hour < 0 || hour > 23) return 0;
    return s->stops[stop].hour_arrivals[hour];
}

Status sim_bus_motion(const Sim *s, size_t bus, SimBusMotion *out) {
    if (!s || !out) return STATUS_ERR_NULL;
    if (bus >= s->n_buses) return STATUS_ERR_INVALID;

    const SimBus *b = &s->buses[bus];
    out->moving = b->motion_moving;
    out->route = b->motion_route;
    out->leg_index = b->motion_leg_index;
    out->from_stop = b->motion_from_stop;
    out->to_stop = b->motion_to_stop;
    out->depart_s = b->motion_depart_s;
    out->arrive_s = b->motion_arrive_s;
    return STATUS_OK;
}

Status sim_set_loop_observer(Sim *s, SimLoopFn fn, void *ctx) {
    if (!s) return STATUS_ERR_NULL;
    s->loop_fn = fn;
    s->loop_ctx = ctx;
    return STATUS_OK;
}
