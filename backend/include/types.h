#ifndef TYPES_H
#define TYPES_H

/**
 * TIME AND PHYSICAL UNIT CONVENTIONS:
 * - Simulation time: double in MINUTES since midnight (e.g. 480.0 = 08:00 AM).
 * - Schedule departure times: int in MINUTES since midnight (e.g. 480 = 08:00 AM).
 * - Distances: double in METRES.
 * - Edge base_time: double in SECONDS.
 * - Coordinates: double latitude/longitude in decimal degrees.
 */

#include <stddef.h>
#include "common.h"
#include "linkedlist.h"
#include "queue.h"

#define STOP_NAME_MAX 64
#define DEPOT_NAME_MAX 64
#define BUS_REG_MAX 16
#define ROUTE_NAME_MAX 64

typedef enum BusStatus {
    BUS_IDLE = 0,
    BUS_RUNNING,
    BUS_BROKEN
} BusStatus;

typedef enum StopType {
    STOP_HUB = 0,
    STOP_OFFICE,
    STOP_MARKET,
    STOP_RESIDENTIAL
} StopType;

typedef enum ScenarioType {
    SCENARIO_NONE = 0,
    SCENARIO_RUSH_HOUR,
    SCENARIO_RAIN,
    SCENARIO_BLOCK_ROAD,
    SCENARIO_BREAKDOWN,
    SCENARIO_EVENT_SPIKE
} ScenarioType;

typedef enum DayType {
    DAY_WEEKDAY = 0,
    DAY_WEEKEND,
    DAY_HOLIDAY
} DayType;

typedef struct Passenger {
    int id;
    int stop_id;
    double arrival_time;
} Passenger;

typedef struct Stop {
    int id;
    char name[STOP_NAME_MAX];
    double lat;
    double lng;
    StopType type;
    Queue *queue;
} Stop;

typedef struct Depot {
    int id;
    char name[DEPOT_NAME_MAX];
    double lat;
    double lng;
    LinkedList *buses; /* holds Bus*; does NOT own buses */
} Depot;

typedef struct JourneyRecord {
    int trip_id;
    int route_id;
    double start_time;
    double end_time;
    int passengers_carried;
} JourneyRecord;

typedef struct Bus {
    int id;
    char registration[BUS_REG_MAX];
    int capacity;
    int depot_id;
    BusStatus status;
    int current_route_id;
    double lat;
    double lng;
    int load;
    LinkedList *journey; /* owns JourneyRecord* */
} Bus;

typedef struct Route {
    int id;
    char name[ROUTE_NAME_MAX];
    LinkedList *stop_ids; /* holds int* items, owned */
    int assigned_buses;
    double predicted_demand;
    double priority;
} Route;

typedef struct Edge {
    int from;
    int to;
    double length_m;
    double base_time_s;
    double traffic_factor;
    int blocked;
} Edge;

typedef struct Trip {
    int trip_id;
    int route_id;
    int bus_id;
    int departure_time;
} Trip;

typedef struct DemandRecord {
    int stop_id;
    int hour;
    DayType day_type;
    int rain_flag;
    int event_flag;
    double passengers;
} DemandRecord;

/* Enum string converters */
const char *bus_status_to_string(BusStatus s);
const char *stop_type_to_string(StopType t);
const char *scenario_type_to_string(ScenarioType s);
const char *day_type_to_string(DayType d);

/* Constructors and destructors */
Passenger *passenger_create(int id, int stop_id, double arrival_time);
void passenger_destroy(Passenger *p);

Stop *stop_create(int id, const char *name, double lat, double lng, StopType type);
void stop_destroy(Stop *s);

Depot *depot_create(int id, const char *name, double lat, double lng);
void depot_destroy(Depot *d);

JourneyRecord *journey_record_create(int trip_id, int route_id, double start_time, double end_time, int passengers_carried);
void journey_record_destroy(JourneyRecord *j);

Bus *bus_create(int id, const char *registration, int capacity, int depot_id);
void bus_destroy(Bus *b);

Route *route_create(int id, const char *name, double predicted_demand);
void route_destroy(Route *r);

Trip *trip_create(int trip_id, int route_id, int bus_id, int departure_time);
void trip_destroy(Trip *t);

Edge *edge_create(int from, int to, double length_m, double base_time_s);
void edge_destroy(Edge *e);

DemandRecord *demand_record_create(int stop_id, int hour, DayType day_type, int rain_flag, int event_flag, double passengers);
void demand_record_destroy(DemandRecord *dr);

/* Helper functions */

/**
 * Comparator for trips, ordering by departure_time ascending, then trip_id ascending.
 * Used for AVL schedule tree.
 */
int trip_cmp(const void *a, const void *b);

/**
 * Calculates dynamic priority for a route: predicted_demand / (assigned_buses + 1).
 */
double route_compute_priority(const Route *r);

/**
 * Comparator for route max-heap: higher priority routes return cmp < 0.
 */
int route_priority_cmp_max(const void *a, const void *b);

/**
 * Adds a stop ID to the route's stop sequence, allocating an owned int*.
 */
Status route_add_stop(Route *r, int stop_id);

#endif /* TYPES_H */

