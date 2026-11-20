#include "types.h"
#include <stdlib.h>
#include <string.h>

const char *bus_status_to_string(BusStatus s) {
    switch (s) {
        case BUS_IDLE:
            return "BUS_IDLE";
        case BUS_RUNNING:
            return "BUS_RUNNING";
        case BUS_BROKEN:
            return "BUS_BROKEN";
        default:
            return "BUS_UNKNOWN";
    }
}

const char *stop_type_to_string(StopType t) {
    switch (t) {
        case STOP_HUB:
            return "STOP_HUB";
        case STOP_OFFICE:
            return "STOP_OFFICE";
        case STOP_MARKET:
            return "STOP_MARKET";
        case STOP_RESIDENTIAL:
            return "STOP_RESIDENTIAL";
        default:
            return "STOP_UNKNOWN";
    }
}

const char *scenario_type_to_string(ScenarioType s) {
    switch (s) {
        case SCENARIO_NONE:
            return "SCENARIO_NONE";
        case SCENARIO_RUSH_HOUR:
            return "SCENARIO_RUSH_HOUR";
        case SCENARIO_RAIN:
            return "SCENARIO_RAIN";
        case SCENARIO_BLOCK_ROAD:
            return "SCENARIO_BLOCK_ROAD";
        case SCENARIO_BREAKDOWN:
            return "SCENARIO_BREAKDOWN";
        case SCENARIO_EVENT_SPIKE:
            return "SCENARIO_EVENT_SPIKE";
        default:
            return "SCENARIO_UNKNOWN";
    }
}

const char *day_type_to_string(DayType d) {
    switch (d) {
        case DAY_WEEKDAY:
            return "DAY_WEEKDAY";
        case DAY_WEEKEND:
            return "DAY_WEEKEND";
        case DAY_HOLIDAY:
            return "DAY_HOLIDAY";
        default:
            return "DAY_UNKNOWN";
    }
}

Passenger *passenger_create(int id, int stop_id, double arrival_time) {
    Passenger *p = (Passenger *)malloc(sizeof(Passenger));
    if (!p) {
        return NULL;
    }
    p->id = id;
    p->stop_id = stop_id;
    p->arrival_time = arrival_time;
    return p;
}

void passenger_destroy(Passenger *p) {
    if (!p) {
        return;
    }
    free(p);
}

Stop *stop_create(int id, const char *name, double lat, double lng, StopType type) {
    Stop *s = (Stop *)malloc(sizeof(Stop));
    if (!s) {
        return NULL;
    }
    s->id = id;
    if (name) {
        strncpy(s->name, name, STOP_NAME_MAX - 1);
        s->name[STOP_NAME_MAX - 1] = '\0';
    } else {
        s->name[0] = '\0';
    }
    s->lat = lat;
    s->lng = lng;
    s->type = type;
    s->queue = queue_create((FreeFn)passenger_destroy);
    if (!s->queue) {
        free(s);
        return NULL;
    }
    return s;
}

void stop_destroy(Stop *s) {
    if (!s) {
        return;
    }
    queue_destroy(s->queue);
    free(s);
}

Depot *depot_create(int id, const char *name, double lat, double lng) {
    Depot *d = (Depot *)malloc(sizeof(Depot));
    if (!d) {
        return NULL;
    }
    d->id = id;
    if (name) {
        strncpy(d->name, name, DEPOT_NAME_MAX - 1);
        d->name[DEPOT_NAME_MAX - 1] = '\0';
    } else {
        d->name[0] = '\0';
    }
    d->lat = lat;
    d->lng = lng;
    d->buses = ll_create(NULL);
    if (!d->buses) {
        free(d);
        return NULL;
    }
    return d;
}

void depot_destroy(Depot *d) {
    if (!d) {
        return;
    }
    ll_destroy(d->buses);
    free(d);
}

JourneyRecord *journey_record_create(int trip_id, int route_id, double start_time, double end_time, int passengers_carried) {
    JourneyRecord *j = (JourneyRecord *)malloc(sizeof(JourneyRecord));
    if (!j) {
        return NULL;
    }
    j->trip_id = trip_id;
    j->route_id = route_id;
    j->start_time = start_time;
    j->end_time = end_time;
    j->passengers_carried = passengers_carried;
    return j;
}

void journey_record_destroy(JourneyRecord *j) {
    if (!j) {
        return;
    }
    free(j);
}

Bus *bus_create(int id, const char *registration, int capacity, int depot_id) {
    Bus *b = (Bus *)malloc(sizeof(Bus));
    if (!b) {
        return NULL;
    }
    b->id = id;
    if (registration) {
        strncpy(b->registration, registration, BUS_REG_MAX - 1);
        b->registration[BUS_REG_MAX - 1] = '\0';
    } else {
        b->registration[0] = '\0';
    }
    b->capacity = capacity;
    b->depot_id = depot_id;
    b->status = BUS_IDLE;
    b->current_route_id = -1;
    b->lat = 0.0;
    b->lng = 0.0;
    b->load = 0;
    b->journey = ll_create((FreeFn)journey_record_destroy);
    if (!b->journey) {
        free(b);
        return NULL;
    }
    return b;
}

void bus_destroy(Bus *b) {
    if (!b) {
        return;
    }
    ll_destroy(b->journey);
    free(b);
}

Route *route_create(int id, const char *name, double predicted_demand) {
    Route *r = (Route *)malloc(sizeof(Route));
    if (!r) {
        return NULL;
    }
    r->id = id;
    if (name) {
        strncpy(r->name, name, ROUTE_NAME_MAX - 1);
        r->name[ROUTE_NAME_MAX - 1] = '\0';
    } else {
        r->name[0] = '\0';
    }
    r->assigned_buses = 0;
    r->predicted_demand = predicted_demand;
    r->priority = route_compute_priority(r);
    r->stop_ids = ll_create(free);
    if (!r->stop_ids) {
        free(r);
        return NULL;
    }
    return r;
}

void route_destroy(Route *r) {
    if (!r) {
        return;
    }
    ll_destroy(r->stop_ids);
    free(r);
}

Trip *trip_create(int trip_id, int route_id, int bus_id, int departure_time) {
    Trip *t = (Trip *)malloc(sizeof(Trip));
    if (!t) {
        return NULL;
    }
    t->trip_id = trip_id;
    t->route_id = route_id;
    t->bus_id = bus_id;
    t->departure_time = departure_time;
    return t;
}

void trip_destroy(Trip *t) {
    if (!t) {
        return;
    }
    free(t);
}

Edge *edge_create(int from, int to, double length_m, double base_time_s) {
    Edge *e = (Edge *)malloc(sizeof(Edge));
    if (!e) {
        return NULL;
    }
    e->from = from;
    e->to = to;
    e->length_m = length_m;
    e->base_time_s = base_time_s;
    e->traffic_factor = 1.0;
    e->blocked = 0;
    return e;
}

void edge_destroy(Edge *e) {
    if (!e) {
        return;
    }
    free(e);
}

DemandRecord *demand_record_create(int stop_id, int hour, DayType day_type, int rain_flag, int event_flag, double passengers) {
    DemandRecord *dr = (DemandRecord *)malloc(sizeof(DemandRecord));
    if (!dr) {
        return NULL;
    }
    dr->stop_id = stop_id;
    dr->hour = hour;
    dr->day_type = day_type;
    dr->rain_flag = rain_flag;
    dr->event_flag = event_flag;
    dr->passengers = passengers;
    return dr;
}

void demand_record_destroy(DemandRecord *dr) {
    if (!dr) {
        return;
    }
    free(dr);
}

int trip_cmp(const void *a, const void *b) {
    const Trip *ta = (const Trip *)a;
    const Trip *tb = (const Trip *)b;
    if (ta->departure_time < tb->departure_time) {
        return -1;
    }
    if (ta->departure_time > tb->departure_time) {
        return 1;
    }
    if (ta->trip_id < tb->trip_id) {
        return -1;
    }
    if (ta->trip_id > tb->trip_id) {
        return 1;
    }
    return 0;
}

double route_compute_priority(const Route *r) {
    if (!r) {
        return 0.0;
    }
    return r->predicted_demand / (double)(r->assigned_buses + 1);
}

int route_priority_cmp_max(const void *a, const void *b) {
    const Route *ra = (const Route *)a;
    const Route *rb = (const Route *)b;
    if (ra->priority > rb->priority) {
        return -1;
    }
    if (ra->priority < rb->priority) {
        return 1;
    }
    if (ra->id < rb->id) {
        return -1;
    }
    if (ra->id > rb->id) {
        return 1;
    }
    return 0;
}

Status route_add_stop(Route *r, int stop_id) {
    if (!r || !r->stop_ids) {
        return STATUS_ERR_NULL;
    }
    int *p = (int *)malloc(sizeof(int));
    if (!p) {
        return STATUS_ERR_NOMEM;
    }
    *p = stop_id;
    Status s = ll_append(r->stop_ids, p);
    if (s != STATUS_OK) {
        free(p);
        return s;
    }
    return STATUS_OK;
}

