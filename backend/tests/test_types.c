#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_util.h"
#include "types.h"
#include "heap.h"

static void test_types_defaults_and_strings(void) {
    /* Enum string converters */
    TEST_ASSERT_EQ_INT(strcmp(bus_status_to_string(BUS_IDLE), "BUS_IDLE"), 0);
    TEST_ASSERT_EQ_INT(strcmp(bus_status_to_string(BUS_RUNNING), "BUS_RUNNING"), 0);
    TEST_ASSERT_EQ_INT(strcmp(bus_status_to_string(BUS_BROKEN), "BUS_BROKEN"), 0);

    TEST_ASSERT_EQ_INT(strcmp(stop_type_to_string(STOP_HUB), "STOP_HUB"), 0);
    TEST_ASSERT_EQ_INT(strcmp(stop_type_to_string(STOP_OFFICE), "STOP_OFFICE"), 0);
    TEST_ASSERT_EQ_INT(strcmp(stop_type_to_string(STOP_MARKET), "STOP_MARKET"), 0);
    TEST_ASSERT_EQ_INT(strcmp(stop_type_to_string(STOP_RESIDENTIAL), "STOP_RESIDENTIAL"), 0);

    TEST_ASSERT_EQ_INT(strcmp(scenario_type_to_string(SCENARIO_NONE), "SCENARIO_NONE"), 0);
    TEST_ASSERT_EQ_INT(strcmp(scenario_type_to_string(SCENARIO_RUSH_HOUR), "SCENARIO_RUSH_HOUR"), 0);
    TEST_ASSERT_EQ_INT(strcmp(scenario_type_to_string(SCENARIO_RAIN), "SCENARIO_RAIN"), 0);
    TEST_ASSERT_EQ_INT(strcmp(scenario_type_to_string(SCENARIO_BLOCK_ROAD), "SCENARIO_BLOCK_ROAD"), 0);
    TEST_ASSERT_EQ_INT(strcmp(scenario_type_to_string(SCENARIO_BREAKDOWN), "SCENARIO_BREAKDOWN"), 0);
    TEST_ASSERT_EQ_INT(strcmp(scenario_type_to_string(SCENARIO_EVENT_SPIKE), "SCENARIO_EVENT_SPIKE"), 0);

    TEST_ASSERT_EQ_INT(strcmp(day_type_to_string(DAY_WEEKDAY), "DAY_WEEKDAY"), 0);
    TEST_ASSERT_EQ_INT(strcmp(day_type_to_string(DAY_WEEKEND), "DAY_WEEKEND"), 0);
    TEST_ASSERT_EQ_INT(strcmp(day_type_to_string(DAY_HOLIDAY), "DAY_HOLIDAY"), 0);

    /* Edge defaults */
    Edge *e = edge_create(1, 2, 1500.0, 180.0);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQ_INT(e->from, 1);
    TEST_ASSERT_EQ_INT(e->to, 2);
    TEST_ASSERT(e->length_m == 1500.0);
    TEST_ASSERT(e->base_time_s == 180.0);
    TEST_ASSERT(e->traffic_factor == 1.0);
    TEST_ASSERT_EQ_INT(e->blocked, 0);
    edge_destroy(e);

    /* Demand record defaults */
    DemandRecord *dr = demand_record_create(101, 8, DAY_WEEKDAY, 0, 0, 150.0);
    TEST_ASSERT_NOT_NULL(dr);
    TEST_ASSERT_EQ_INT(dr->stop_id, 101);
    TEST_ASSERT_EQ_INT(dr->hour, 8);
    TEST_ASSERT_EQ_INT(dr->day_type, DAY_WEEKDAY);
    TEST_ASSERT(dr->passengers == 150.0);
    demand_record_destroy(dr);
}

static void test_stop_destroy_frees_passengers(void) {
    Stop *s = stop_create(1, "Majestic", 12.9767, 77.5713, STOP_HUB);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQ_INT(strcmp(s->name, "Majestic"), 0);
    TEST_ASSERT_NOT_NULL(s->queue);

    Passenger *p1 = passenger_create(1, 1, 480.0);
    Passenger *p2 = passenger_create(2, 1, 485.0);
    Passenger *p3 = passenger_create(3, 1, 490.0);

    TEST_ASSERT_EQ_INT(queue_enqueue(s->queue, p1), STATUS_OK);
    TEST_ASSERT_EQ_INT(queue_enqueue(s->queue, p2), STATUS_OK);
    TEST_ASSERT_EQ_INT(queue_enqueue(s->queue, p3), STATUS_OK);
    TEST_ASSERT_EQ_INT(queue_size(s->queue), 3);

    /* stop_destroy must free the queue and all 3 remaining passengers */
    stop_destroy(s);
}

static void test_bus_destroy_frees_journey(void) {
    Bus *b = bus_create(101, "KA-01-F-1234", 45, 1);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQ_INT(strcmp(b->registration, "KA-01-F-1234"), 0);
    TEST_ASSERT_EQ_INT(b->status, BUS_IDLE);
    TEST_ASSERT_EQ_INT(b->current_route_id, -1);
    TEST_ASSERT_NOT_NULL(b->journey);

    JourneyRecord *j1 = journey_record_create(1, 10, 480.0, 540.0, 42);
    JourneyRecord *j2 = journey_record_create(2, 10, 560.0, 620.0, 38);
    TEST_ASSERT_EQ_INT(ll_append(b->journey, j1), STATUS_OK);
    TEST_ASSERT_EQ_INT(ll_append(b->journey, j2), STATUS_OK);
    TEST_ASSERT_EQ_INT(ll_size(b->journey), 2);

    /* bus_destroy must free the journey list and both journey records */
    bus_destroy(b);
}

static void test_depot_destroy_does_not_free_buses(void) {
    Bus *b1 = bus_create(201, "KA-01-F-2001", 50, 10);
    Bus *b2 = bus_create(202, "KA-01-F-2002", 50, 10);
    TEST_ASSERT_NOT_NULL(b1);
    TEST_ASSERT_NOT_NULL(b2);

    Depot *d = depot_create(10, "Shantinagar", 12.9552, 77.5977);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_EQ_INT(ll_append(d->buses, b1), STATUS_OK);
    TEST_ASSERT_EQ_INT(ll_append(d->buses, b2), STATUS_OK);
    TEST_ASSERT_EQ_INT(ll_size(d->buses), 2);

    /* depot_destroy must free the list structure, but NOT the bus objects */
    depot_destroy(d);

    /* b1 and b2 must still be fully intact and valid */
    TEST_ASSERT_EQ_INT(b1->id, 201);
    TEST_ASSERT_EQ_INT(b2->id, 202);
    TEST_ASSERT_EQ_INT(strcmp(b1->registration, "KA-01-F-2001"), 0);

    bus_destroy(b1);
    bus_destroy(b2);
}

static void test_route_and_stops(void) {
    Route *r = route_create(500, "500-D", 1200.0);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQ_INT(r->id, 500);
    TEST_ASSERT_EQ_INT(r->assigned_buses, 0);
    TEST_ASSERT(r->priority == 1200.0);

    TEST_ASSERT_EQ_INT(route_add_stop(r, 10), STATUS_OK);
    TEST_ASSERT_EQ_INT(route_add_stop(r, 20), STATUS_OK);
    TEST_ASSERT_EQ_INT(route_add_stop(r, 30), STATUS_OK);
    TEST_ASSERT_EQ_INT(ll_size(r->stop_ids), 3);

    TEST_ASSERT_EQ_INT(*(int *)ll_get(r->stop_ids, 0), 10);
    TEST_ASSERT_EQ_INT(*(int *)ll_get(r->stop_ids, 1), 20);
    TEST_ASSERT_EQ_INT(*(int *)ll_get(r->stop_ids, 2), 30);

    route_destroy(r);
}

static void test_trip_cmp(void) {
    Trip t1 = { .trip_id = 10, .route_id = 1, .bus_id = 1, .departure_time = 480 };
    Trip t2 = { .trip_id = 20, .route_id = 1, .bus_id = 2, .departure_time = 500 };
    Trip t3 = { .trip_id = 5,  .route_id = 1, .bus_id = 3, .departure_time = 480 };
    Trip t4 = { .trip_id = 10, .route_id = 1, .bus_id = 1, .departure_time = 480 };

    /* Different departure times */
    TEST_ASSERT(trip_cmp(&t1, &t2) < 0);
    TEST_ASSERT(trip_cmp(&t2, &t1) > 0);

    /* Equal departure times: ordered by trip_id */
    TEST_ASSERT(trip_cmp(&t3, &t1) < 0);
    TEST_ASSERT(trip_cmp(&t1, &t3) > 0);

    /* Identical */
    TEST_ASSERT_EQ_INT(trip_cmp(&t1, &t4), 0);
}

static void test_route_priority_max_heap(void) {
    Route *r1 = route_create(1, "Route 1", 300.0); /* assigned 0 -> prio 300 / 1 = 300.0 */
    Route *r2 = route_create(2, "Route 2", 800.0);
    r2->assigned_buses = 1;                        /* assigned 1 -> prio 800 / 2 = 400.0 */
    r2->priority = route_compute_priority(r2);

    Route *r3 = route_create(3, "Route 3", 1000.0);
    r3->assigned_buses = 4;                        /* assigned 4 -> prio 1000 / 5 = 200.0 */
    r3->priority = route_compute_priority(r3);

    Route *r4 = route_create(4, "Route 4", 500.0);
    r4->assigned_buses = 0;                        /* assigned 0 -> prio 500 / 1 = 500.0 */
    r4->priority = route_compute_priority(r4);

    /* Expected priorities: r4 (500), r2 (400), r1 (300), r3 (200) */
    Heap *h = heap_create(route_priority_cmp_max, NULL, NULL);
    TEST_ASSERT_NOT_NULL(h);

    heap_push(h, r1);
    heap_push(h, r2);
    heap_push(h, r3);
    heap_push(h, r4);
    TEST_ASSERT_EQ_INT(heap_size(h), 4);

    Route *first = (Route *)heap_pop(h);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQ_INT(first->id, 4);

    Route *second = (Route *)heap_pop(h);
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_EQ_INT(second->id, 2);

    Route *third = (Route *)heap_pop(h);
    TEST_ASSERT_NOT_NULL(third);
    TEST_ASSERT_EQ_INT(third->id, 1);

    Route *fourth = (Route *)heap_pop(h);
    TEST_ASSERT_NOT_NULL(fourth);
    TEST_ASSERT_EQ_INT(fourth->id, 3);

    heap_destroy(h);
    route_destroy(r1);
    route_destroy(r2);
    route_destroy(r3);
    route_destroy(r4);
}

int main(void) {
    RUN_TEST(test_types_defaults_and_strings);
    RUN_TEST(test_stop_destroy_frees_passengers);
    RUN_TEST(test_bus_destroy_frees_journey);
    RUN_TEST(test_depot_destroy_does_not_free_buses);
    RUN_TEST(test_route_and_stops);
    RUN_TEST(test_trip_cmp);
    RUN_TEST(test_route_priority_max_heap);
    return test_summary();
}

