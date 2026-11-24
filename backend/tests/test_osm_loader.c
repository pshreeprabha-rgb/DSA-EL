#include "test_util.h"
#include "osm_loader.h"
#include "graph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ASSERT_TRUE(cond) TEST_ASSERT(cond)
#define ASSERT_EQ(a,b)    TEST_ASSERT_EQ_INT(a,b)
#define ASSERT_NULL(p)    TEST_ASSERT_NULL(p)
#define ASSERT_NOT_NULL(p) TEST_ASSERT_NOT_NULL(p)
#define ASSERT_DOUBLE_EQ(a,b,tol) TEST_ASSERT(fabs((a)-(b))<=(tol))

/*
 * Fixture:
 * Nodes:
 *   1: lat=12.00, lng=77.00
 *   2: lat=12.01, lng=77.01  (not a junction, interior of way1)
 *   3: lat=12.02, lng=77.02  (junction: shared by way1,way2,way3)
 *   4: lat=12.03, lng=77.03  (not a junction, interior of way2)
 *   5: lat=12.04, lng=77.04  (endpoint way2)
 *   6: lat=12.05, lng=77.05  (endpoint way3)
 *   10: lat=20.00, lng=80.00 (island node1)
 *   11: lat=20.01, lng=80.01 (island node2)
 *   99: lat=12.08, lng=77.08 (missing from way5 - just to test)
 *
 * Ways:
 *   way1: primary two-way, nodes [1,2,3]
 *   way2: secondary oneway=yes, nodes [3,4,5]
 *   way3: tertiary oneway=-1, nodes [3,6]
 *   way4: island [10,11]
 *   way5: has roundabout junction tag, nodes [1,3]
 *   way_bad: references missing node 99 (node 99 not in elements)
 *
 * Bus stops:
 *   stopA: near node 1 (within 150m) -> snapped, sets is_stop
 *   stopB: far away (> 150m)         -> discarded
 */

static const char *FIXTURE_ROADS_JSON =
"{"
"\"elements\":["
/* nodes */
"{\"type\":\"node\",\"id\":1,\"lat\":12.00,\"lon\":77.00},"
"{\"type\":\"node\",\"id\":2,\"lat\":12.01,\"lon\":77.01},"
"{\"type\":\"node\",\"id\":3,\"lat\":12.02,\"lon\":77.02},"
"{\"type\":\"node\",\"id\":4,\"lat\":12.03,\"lon\":77.03},"
"{\"type\":\"node\",\"id\":5,\"lat\":12.04,\"lon\":77.04},"
"{\"type\":\"node\",\"id\":6,\"lat\":12.05,\"lon\":77.05},"
"{\"type\":\"node\",\"id\":10,\"lat\":20.00,\"lon\":80.00},"
"{\"type\":\"node\",\"id\":11,\"lat\":20.01,\"lon\":80.01},"
/* way1: primary bidirectional [1,2,3] */
"{\"type\":\"way\",\"id\":101,"
  "\"nodes\":[1,2,3],"
  "\"tags\":{\"highway\":\"primary\",\"name\":\"Way One\"}},"
/* way2: secondary oneway [3,4,5] */
"{\"type\":\"way\",\"id\":102,"
  "\"nodes\":[3,4,5],"
  "\"tags\":{\"highway\":\"secondary\",\"name\":\"Way Two\",\"oneway\":\"yes\",\"maxspeed\":\"50\"}},"
/* way3: tertiary oneway=-1 [3,6] */
"{\"type\":\"way\",\"id\":103,"
  "\"nodes\":[3,6],"
  "\"tags\":{\"highway\":\"tertiary\",\"name\":\"Way Three\",\"oneway\":\"-1\"}},"
/* way4: island [10,11] */
"{\"type\":\"way\",\"id\":104,"
  "\"nodes\":[10,11],"
  "\"tags\":{\"highway\":\"primary\",\"name\":\"Island Way\"}},"
/* way5: roundabout [1,3] */
"{\"type\":\"way\",\"id\":105,"
  "\"nodes\":[1,3],"
  "\"tags\":{\"highway\":\"primary\",\"junction\":\"roundabout\"}},"
/* way_bad: references missing node */
"{\"type\":\"way\",\"id\":106,"
  "\"nodes\":[1,999],"
  "\"tags\":{\"highway\":\"primary\"}}"
"]}";

static const char *FIXTURE_STOPS_JSON =
"{"
"\"elements\":["
/* stopA: near node 1 (lat=12.00,lng=77.00), within 150m */
"{\"type\":\"node\",\"id\":201,\"lat\":12.0005,\"lon\":77.0005,"
  "\"tags\":{\"highway\":\"bus_stop\",\"name\":\"Stop A\"}},"
/* stopB: far from all graph nodes */
"{\"type\":\"node\",\"id\":202,\"lat\":15.00,\"lon\":80.00,"
  "\"tags\":{\"highway\":\"bus_stop\",\"name\":\"Stop B Far\"}}"
"]}";

static void test_junction_detection_and_edges(void) {
    Graph *g = NULL;
    OsmBuildStats stats;
    Status st = osm_build_graph_from_json(FIXTURE_ROADS_JSON, NULL, &g, &stats);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_NOT_NULL(g);

    /* node 2 and node 4 should NOT be in the graph (not junctions) */
    ASSERT_EQ(graph_find_node_by_osm_id(g, 2), -1);
    ASSERT_EQ(graph_find_node_by_osm_id(g, 4), -1);

    /* Nodes 1, 3, 5, 6 should be in the graph (junctions/endpoints) */
    /* After LCC extraction, island nodes 10,11 are dropped */
    int idx1 = graph_find_node_by_osm_id(g, 1);
    int idx3 = graph_find_node_by_osm_id(g, 3);
    int idx5 = graph_find_node_by_osm_id(g, 5);
    int idx6 = graph_find_node_by_osm_id(g, 6);
    ASSERT_TRUE(idx1 >= 0);
    ASSERT_TRUE(idx3 >= 0);
    ASSERT_TRUE(idx5 >= 0);
    ASSERT_TRUE(idx6 >= 0);

    /* Island dropped by LCC */
    ASSERT_TRUE(stats.nodes_dropped_by_component >= 2);
    ASSERT_EQ(graph_find_node_by_osm_id(g, 10), -1);
    ASSERT_EQ(graph_find_node_by_osm_id(g, 11), -1);

    /* way1 edge: 1<->3 length = haversine(1,2)+haversine(2,3) */
    double h12 = graph_haversine_m(12.00, 77.00, 12.01, 77.01);
    double h23 = graph_haversine_m(12.01, 77.01, 12.02, 77.02);
    double expected_len = h12 + h23;

    int found_fwd = 0, found_bwd = 0;
    for (size_t e = 0; e < graph_edge_count(g); e++) {
        const Edge *edge = graph_edge(g, (int)e);
        if (!edge) continue;
        if (edge->from == idx1 && edge->to == idx3) {
            ASSERT_DOUBLE_EQ(edge->length_m, expected_len, 1.0);
            found_fwd = 1;
        }
        if (edge->from == idx3 && edge->to == idx1) {
            ASSERT_DOUBLE_EQ(edge->length_m, expected_len, 1.0);
            found_bwd = 1;
        }
    }
    ASSERT_TRUE(found_fwd);
    ASSERT_TRUE(found_bwd);

    /* way2: oneway=yes 3->5 only */
    int found35 = 0, found53 = 0;
    for (size_t e = 0; e < graph_edge_count(g); e++) {
        const Edge *edge = graph_edge(g, (int)e);
        if (!edge) continue;
        if (edge->from == idx3 && edge->to == idx5) found35 = 1;
        if (edge->from == idx5 && edge->to == idx3) found53 = 1;
    }
    ASSERT_TRUE(found35);
    ASSERT_EQ(found53, 0);

    /* way3: oneway=-1 so only 6->3 */
    int found63 = 0, found36 = 0;
    for (size_t e = 0; e < graph_edge_count(g); e++) {
        const Edge *edge = graph_edge(g, (int)e);
        if (!edge) continue;
        if (edge->from == idx6 && edge->to == idx3) found63 = 1;
        if (edge->from == idx3 && edge->to == idx6) found36 = 1;
    }
    ASSERT_TRUE(found63);
    ASSERT_EQ(found36, 0);

    /* way5: roundabout - only forward 1->3 */
    int found13_round = 0, found31_round = 0;
    /* Count: could be 1->3 from both way1 and way5; way5 must be one-directional */
    /* Check that we don't have extra 3->1 edge from roundabout */
    int count31 = 0;
    for (size_t e = 0; e < graph_edge_count(g); e++) {
        const Edge *edge = graph_edge(g, (int)e);
        if (!edge) continue;
        if (edge->from == idx1 && edge->to == idx3) found13_round = 1;
        if (edge->from == idx3 && edge->to == idx1) count31++;
        (void)found31_round;
    }
    ASSERT_TRUE(found13_round);
    /* At most 1 edge from way1 (bidirectional gives one 3->1) */
    ASSERT_TRUE(count31 <= 1);

    /* maxspeed "50" -> base_time = length / (50/3.6) */
    for (size_t e = 0; e < graph_edge_count(g); e++) {
        const Edge *edge = graph_edge(g, (int)e);
        const EdgeMeta *meta = graph_edge_meta(g, (int)e);
        if (!edge || !meta) continue;
        if (edge->from == idx3 && edge->to == idx5) {
            double h34 = graph_haversine_m(12.02, 77.02, 12.03, 77.03);
            double h45 = graph_haversine_m(12.03, 77.03, 12.04, 77.04);
            double expected_bt = (h34 + h45) / (50.0 / 3.6);
            ASSERT_DOUBLE_EQ(edge->base_time_s, expected_bt, 1.0);
        }
    }

    /* 3c) OsmBuildStats exact assertions against fixture */
    ASSERT_EQ(stats.ways_total, 6);
    ASSERT_EQ(stats.ways_used, 5); /* 5 ways produced edges; way 106 has missing node 999 so skipped */
    ASSERT_EQ(stats.json_nodes, 8); /* nodes 1,2,3,4,5,6,10,11 */
    ASSERT_EQ(stats.junction_nodes, 6); /* nodes 1,3,5,6 in main, 10,11 in island */
    ASSERT_EQ(stats.nodes_dropped_by_component, 2); /* island nodes 10, 11 */
    ASSERT_EQ(stats.final_nodes, 4); /* nodes 1, 3, 5, 6 */
    ASSERT_EQ(stats.final_edges, 5); /* 1->3 (way1 fwd), 3->1 (way1 bwd), 3->5 (way2), 6->3 (way3), 1->3 (way5) */

    graph_destroy(g);
}

static void test_osm_speeds_and_clamping(void) {
    /* Test default speeds:
     * primary = 35 km/h
     * secondary = 30 km/h
     * tertiary = 25 km/h
     * primary_link / secondary_link / tertiary_link = 20 km/h
     * And clamping:
     * 5 -> 10 km/h
     * 120 -> 80 km/h
     * "50 mph" -> ignored (defaults to class speed)
     * "none" -> ignored
     * "" -> ignored
     */
    const char *speeds_json =
        "{\"elements\":["
        "{\"type\":\"node\",\"id\":1,\"lat\":12.0,\"lon\":77.0},"
        "{\"type\":\"node\",\"id\":2,\"lat\":12.01,\"lon\":77.01},"
        "{\"type\":\"node\",\"id\":3,\"lat\":12.02,\"lon\":77.02},"
        "{\"type\":\"node\",\"id\":4,\"lat\":12.03,\"lon\":77.03},"
        "{\"type\":\"node\",\"id\":5,\"lat\":12.04,\"lon\":77.04},"
        "{\"type\":\"node\",\"id\":6,\"lat\":12.05,\"lon\":77.05},"
        "{\"type\":\"node\",\"id\":7,\"lat\":12.06,\"lon\":77.06},"
        "{\"type\":\"node\",\"id\":8,\"lat\":12.07,\"lon\":77.07},"
        /* way 1: primary, no maxspeed -> 35 km/h */
        "{\"type\":\"way\",\"id\":11,\"nodes\":[1,2],\"tags\":{\"highway\":\"primary\",\"oneway\":\"yes\"}},"
        /* way 2: secondary, no maxspeed -> 30 km/h */
        "{\"type\":\"way\",\"id\":12,\"nodes\":[2,3],\"tags\":{\"highway\":\"secondary\",\"oneway\":\"yes\"}},"
        /* way 3: tertiary, no maxspeed -> 25 km/h */
        "{\"type\":\"way\",\"id\":13,\"nodes\":[3,4],\"tags\":{\"highway\":\"tertiary\",\"oneway\":\"yes\"}},"
        /* way 4: primary_link -> 20 km/h */
        "{\"type\":\"way\",\"id\":14,\"nodes\":[4,5],\"tags\":{\"highway\":\"primary_link\",\"oneway\":\"yes\"}},"
        /* way 5: maxspeed 5 -> clamped to 10 km/h */
        "{\"type\":\"way\",\"id\":15,\"nodes\":[5,6],\"tags\":{\"highway\":\"primary\",\"oneway\":\"yes\",\"maxspeed\":\"5\"}},"
        /* way 6: maxspeed 120 -> clamped to 80 km/h */
        "{\"type\":\"way\",\"id\":16,\"nodes\":[6,7],\"tags\":{\"highway\":\"primary\",\"oneway\":\"yes\",\"maxspeed\":\"120\"}},"
        /* way 7: maxspeed '50 mph' -> ignored, primary defaults to 35 km/h */
        "{\"type\":\"way\",\"id\":17,\"nodes\":[7,8],\"tags\":{\"highway\":\"primary\",\"oneway\":\"yes\",\"maxspeed\":\"50 mph\"}}"
        "]}";

    Graph *g = NULL;
    OsmBuildStats stats;
    Status st = osm_build_graph_from_json(speeds_json, NULL, &g, &stats);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_NOT_NULL(g);

    for (size_t e = 0; e < graph_edge_count(g); e++) {
        const Edge *edge = graph_edge(g, (int)e);
        if (!edge) continue;
        int u_id = (int)graph_node(g, edge->from)->osm_id;
        int v_id = (int)graph_node(g, edge->to)->osm_id;
        double len = edge->length_m;

        if (u_id == 1 && v_id == 2) {
            /* 35 km/h */
            ASSERT_DOUBLE_EQ(edge->base_time_s, len / (35.0 / 3.6), 1e-4);
        } else if (u_id == 2 && v_id == 3) {
            /* 30 km/h */
            ASSERT_DOUBLE_EQ(edge->base_time_s, len / (30.0 / 3.6), 1e-4);
        } else if (u_id == 3 && v_id == 4) {
            /* 25 km/h */
            ASSERT_DOUBLE_EQ(edge->base_time_s, len / (25.0 / 3.6), 1e-4);
        } else if (u_id == 4 && v_id == 5) {
            /* 20 km/h */
            ASSERT_DOUBLE_EQ(edge->base_time_s, len / (20.0 / 3.6), 1e-4);
        } else if (u_id == 5 && v_id == 6) {
            /* 10 km/h (clamped from 5) */
            ASSERT_DOUBLE_EQ(edge->base_time_s, len / (10.0 / 3.6), 1e-4);
        } else if (u_id == 6 && v_id == 7) {
            /* 80 km/h (clamped from 120) */
            ASSERT_DOUBLE_EQ(edge->base_time_s, len / (80.0 / 3.6), 1e-4);
        } else if (u_id == 7 && v_id == 8) {
            /* 35 km/h (ignored "50 mph") */
            ASSERT_DOUBLE_EQ(edge->base_time_s, len / (35.0 / 3.6), 1e-4);
        }
    }

    graph_destroy(g);
}

static void test_bus_stop_snapping(void) {
    Graph *g = NULL;
    OsmBuildStats stats;
    Status st = osm_build_graph_from_json(FIXTURE_ROADS_JSON, FIXTURE_STOPS_JSON, &g, &stats);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_NOT_NULL(g);

    ASSERT_EQ(stats.stops_found, 2);
    ASSERT_EQ(stats.stops_snapped, 1);
    ASSERT_EQ(stats.stops_discarded, 1);

    /* Node 1 should now be a stop */
    int idx1 = graph_find_node_by_osm_id(g, 1);
    ASSERT_TRUE(idx1 >= 0);
    ASSERT_EQ(graph_node(g, idx1)->is_stop, 1);

    graph_destroy(g);

    /* Two stops on one node test */
    const char *two_stops_json =
        "{\"elements\":["
        "{\"type\":\"node\",\"id\":301,\"lat\":12.0001,\"lon\":77.0001,\"tags\":{\"highway\":\"bus_stop\",\"name\":\"Stop 1\"}},"
        "{\"type\":\"node\",\"id\":302,\"lat\":12.0002,\"lon\":77.0002,\"tags\":{\"highway\":\"bus_stop\",\"name\":\"Stop 2\"}}"
        "]}";
    OsmBuildStats stats2;
    st = osm_build_graph_from_json(FIXTURE_ROADS_JSON, two_stops_json, &g, &stats2);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_NOT_NULL(g);
    ASSERT_EQ(stats2.stops_found, 2);
    ASSERT_EQ(stats2.stops_snapped, 2);
    int idx1_two = graph_find_node_by_osm_id(g, 1);
    ASSERT_TRUE(idx1_two >= 0);
    ASSERT_EQ(graph_node(g, idx1_two)->is_stop, 1);
    graph_destroy(g);
}

static void test_error_cases(void) {
    Graph *g = NULL;

    /* NULL json */
    ASSERT_EQ(osm_build_graph_from_json(NULL, NULL, &g, NULL), STATUS_ERR_NULL);
    ASSERT_NULL(g);

    /* Empty JSON object (no "elements" array) */
    ASSERT_EQ(osm_build_graph_from_json("{}", NULL, &g, NULL), STATUS_ERR_INVALID);
    ASSERT_NULL(g);

    /* Garbage JSON */
    ASSERT_EQ(osm_build_graph_from_json("not json", NULL, &g, NULL), STATUS_ERR_INVALID);
    ASSERT_NULL(g);

    /* Valid JSON but all nodes and ways are on a totally isolated island -> LCC still returns something */
    const char *island_only =
        "{\"elements\":["
        "{\"type\":\"node\",\"id\":1,\"lat\":1.0,\"lon\":1.0},"
        "{\"type\":\"node\",\"id\":2,\"lat\":1.01,\"lon\":1.01},"
        "{\"type\":\"way\",\"id\":1,\"nodes\":[1,2],"
        "\"tags\":{\"highway\":\"primary\"}}"
        "]}";
    Status st = osm_build_graph_from_json(island_only, NULL, &g, NULL);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_NOT_NULL(g);
    graph_destroy(g);
}

int main(void) {
    RUN_TEST(test_junction_detection_and_edges);
    RUN_TEST(test_osm_speeds_and_clamping);
    RUN_TEST(test_bus_stop_snapping);
    RUN_TEST(test_error_cases);
    return test_summary();
}
