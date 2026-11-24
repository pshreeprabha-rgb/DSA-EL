#define _POSIX_C_SOURCE 200809L

#include "graph.h"
#include "hashtable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define EARTH_RADIUS_M 6371000.0

/* Dynamic array for node outgoing edges */
typedef struct OutEdges {
    int *edges;
    size_t count;
    size_t capacity;
} OutEdges;

struct Graph {
    GraphNode *nodes;
    size_t node_count;
    size_t node_capacity;

    OutEdges *adj;            /* size = node_capacity */
    HashTable *osm_to_index;  /* int64_t osm_id -> (void*)(intptr_t)(dense index + 1) */

    Edge *edges;
    EdgeMeta *edge_meta;
    size_t edge_count;
    size_t edge_capacity;

    double max_speed_mps;
};

/* PRNG: splitmix64 */
static uint64_t splitmix64(uint64_t *state) {
    uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static double splitmix64_double(uint64_t *state) {
    return (double)(splitmix64(state) >> 11) * (1.0 / 9007199254740992.0);
}

Graph *graph_create(void) {
    Graph *g = (Graph *)calloc(1, sizeof(Graph));
    if (!g) return NULL;

    g->osm_to_index = ht_create(HT_KEY_INT, NULL);
    if (!g->osm_to_index) {
        free(g);
        return NULL;
    }

    g->max_speed_mps = 1e-3;
    return g;
}

void graph_destroy(Graph *g) {
    if (!g) return;

    if (g->adj) {
        for (size_t i = 0; i < g->node_count; i++) {
            free(g->adj[i].edges);
        }
        free(g->adj);
    }
    free(g->nodes);
    free(g->edges);
    free(g->edge_meta);
    ht_destroy(g->osm_to_index);
    free(g);
}

int graph_add_node(Graph *g, int64_t osm_id, double lat, double lng, const char *name, int is_stop) {
    if (!g) return -1;

    void *val = ht_get_int(g->osm_to_index, osm_id);
    if (val != NULL) {
        int idx = (int)((intptr_t)val - 1);
        if (idx >= 0 && (size_t)idx < g->node_count) {
            if (is_stop) {
                g->nodes[idx].is_stop |= 1;
            }
            if (name && name[0] != '\0' && g->nodes[idx].name[0] == '\0') {
                strncpy(g->nodes[idx].name, name, sizeof(g->nodes[idx].name) - 1);
                g->nodes[idx].name[sizeof(g->nodes[idx].name) - 1] = '\0';
            }
            return idx;
        }
    }

    if (g->node_count >= g->node_capacity) {
        size_t new_cap = (g->node_capacity == 0) ? 16 : g->node_capacity * 2;
        GraphNode *new_nodes = (GraphNode *)realloc(g->nodes, new_cap * sizeof(GraphNode));
        if (!new_nodes) return -1;
        g->nodes = new_nodes;

        OutEdges *new_adj = (OutEdges *)realloc(g->adj, new_cap * sizeof(OutEdges));
        if (!new_adj) return -1;
        for (size_t i = g->node_capacity; i < new_cap; i++) {
            new_adj[i].edges = NULL;
            new_adj[i].count = 0;
            new_adj[i].capacity = 0;
        }
        g->adj = new_adj;
        g->node_capacity = new_cap;
    }

    int idx = (int)g->node_count;
    g->nodes[idx].osm_id = osm_id;
    g->nodes[idx].lat = lat;
    g->nodes[idx].lng = lng;
    g->nodes[idx].is_stop = is_stop ? 1 : 0;
    if (name) {
        strncpy(g->nodes[idx].name, name, sizeof(g->nodes[idx].name) - 1);
        g->nodes[idx].name[sizeof(g->nodes[idx].name) - 1] = '\0';
    } else {
        g->nodes[idx].name[0] = '\0';
    }

    g->adj[idx].edges = NULL;
    g->adj[idx].count = 0;
    g->adj[idx].capacity = 0;

    Status st = ht_put_int(g->osm_to_index, osm_id, (void *)(intptr_t)(idx + 1));
    if (st != STATUS_OK) return -1;

    g->node_count++;
    return idx;
}

int graph_add_edge(Graph *g, int from, int to, double length_m, double base_time_s, const char *road_name, RoadClass cls) {
    if (!g) return -1;
    if (from < 0 || (size_t)from >= g->node_count) return -1;
    if (to < 0 || (size_t)to >= g->node_count) return -1;
    if (from == to) return -1;
    if (length_m <= 0.0 || base_time_s <= 0.0) return -1;

    if (g->edge_count >= g->edge_capacity) {
        size_t new_cap = (g->edge_capacity == 0) ? 16 : g->edge_capacity * 2;
        Edge *new_edges = (Edge *)realloc(g->edges, new_cap * sizeof(Edge));
        if (!new_edges) return -1;
        g->edges = new_edges;

        EdgeMeta *new_meta = (EdgeMeta *)realloc(g->edge_meta, new_cap * sizeof(EdgeMeta));
        if (!new_meta) return -1;
        g->edge_meta = new_meta;
        g->edge_capacity = new_cap;
    }

    int edge_id = (int)g->edge_count;
    g->edges[edge_id].from = from;
    g->edges[edge_id].to = to;
    g->edges[edge_id].length_m = length_m;
    g->edges[edge_id].base_time_s = base_time_s;
    g->edges[edge_id].traffic_factor = 1.0;
    g->edges[edge_id].blocked = 0;

    if (road_name) {
        strncpy(g->edge_meta[edge_id].road_name, road_name, sizeof(g->edge_meta[edge_id].road_name) - 1);
        g->edge_meta[edge_id].road_name[sizeof(g->edge_meta[edge_id].road_name) - 1] = '\0';
    } else {
        g->edge_meta[edge_id].road_name[0] = '\0';
    }
    g->edge_meta[edge_id].road_class = cls;

    /* Add to adjacency list of 'from' */
    OutEdges *oe = &g->adj[from];
    if (oe->count >= oe->capacity) {
        size_t new_cap = (oe->capacity == 0) ? 4 : oe->capacity * 2;
        int *new_arr = (int *)realloc(oe->edges, new_cap * sizeof(int));
        if (!new_arr) return -1;
        oe->edges = new_arr;
        oe->capacity = new_cap;
    }
    oe->edges[oe->count++] = edge_id;

    g->edge_count++;

    /* Update max speed: haversine(from, to) / base_time_s */
    double h_dist = graph_haversine_m(g->nodes[from].lat, g->nodes[from].lng,
                                      g->nodes[to].lat, g->nodes[to].lng);
    double speed = h_dist / base_time_s;
    if (speed > g->max_speed_mps) {
        g->max_speed_mps = speed;
    }

    return edge_id;
}

Status graph_add_bidirectional(Graph *g, int a, int b, double length_m, double base_time_s,
                               const char *road_name, RoadClass cls, int *edge_ab, int *edge_ba) {
    if (!g) return STATUS_ERR_NULL;
    int ab = graph_add_edge(g, a, b, length_m, base_time_s, road_name, cls);
    if (ab < 0) return STATUS_ERR_INVALID;
    int ba = graph_add_edge(g, b, a, length_m, base_time_s, road_name, cls);
    if (ba < 0) return STATUS_ERR_INVALID;

    if (edge_ab) *edge_ab = ab;
    if (edge_ba) *edge_ba = ba;
    return STATUS_OK;
}

size_t graph_node_count(const Graph *g) {
    return g ? g->node_count : 0;
}

size_t graph_edge_count(const Graph *g) {
    return g ? g->edge_count : 0;
}

const GraphNode *graph_node(const Graph *g, int index) {
    if (!g || index < 0 || (size_t)index >= g->node_count) return NULL;
    return &g->nodes[index];
}

int graph_find_node_by_osm_id(const Graph *g, int64_t osm_id) {
    if (!g) return -1;
    void *val = ht_get_int(g->osm_to_index, osm_id);
    if (!val) return -1;
    return (int)((intptr_t)val - 1);
}

const Edge *graph_edge(const Graph *g, int edge_id) {
    if (!g || edge_id < 0 || (size_t)edge_id >= g->edge_count) return NULL;
    return &g->edges[edge_id];
}

Edge *graph_edge_mut(Graph *g, int edge_id) {
    if (!g || edge_id < 0 || (size_t)edge_id >= g->edge_count) return NULL;
    return &g->edges[edge_id];
}

const EdgeMeta *graph_edge_meta(const Graph *g, int edge_id) {
    if (!g || edge_id < 0 || (size_t)edge_id >= g->edge_count) return NULL;
    return &g->edge_meta[edge_id];
}

size_t graph_out_degree(const Graph *g, int node) {
    if (!g || node < 0 || (size_t)node >= g->node_count) return 0;
    return g->adj[node].count;
}

int graph_out_edge(const Graph *g, int node, size_t k) {
    if (!g || node < 0 || (size_t)node >= g->node_count) return -1;
    if (k >= g->adj[node].count) return -1;
    return g->adj[node].edges[k];
}

Status graph_set_traffic_factor(Graph *g, int edge_id, double factor) {
    if (!g) return STATUS_ERR_NULL;
    if (edge_id < 0 || (size_t)edge_id >= g->edge_count) return STATUS_ERR_INVALID;
    if (isnan(factor)) return STATUS_ERR_INVALID;

    if (factor < 1.0) factor = 1.0;
    if (factor > GRAPH_MAX_TRAFFIC_FACTOR) factor = GRAPH_MAX_TRAFFIC_FACTOR;

    g->edges[edge_id].traffic_factor = factor;
    return STATUS_OK;
}

Status graph_set_blocked(Graph *g, int edge_id, int blocked) {
    if (!g) return STATUS_ERR_NULL;
    if (edge_id < 0 || (size_t)edge_id >= g->edge_count) return STATUS_ERR_INVALID;

    g->edges[edge_id].blocked = blocked ? 1 : 0;
    return STATUS_OK;
}

void graph_reset_traffic(Graph *g) {
    if (!g) return;
    for (size_t i = 0; i < g->edge_count; i++) {
        g->edges[i].traffic_factor = 1.0;
    }
}

double graph_edge_cost_s(const Graph *g, int edge_id, CostMode mode) {
    if (!g || edge_id < 0 || (size_t)edge_id >= g->edge_count) return -1.0;
    double base = g->edges[edge_id].base_time_s;
    if (mode == COST_TRAFFIC) {
        return base * g->edges[edge_id].traffic_factor;
    }
    return base;
}

double graph_haversine_m(double lat1, double lng1, double lat2, double lng2) {
    double dlat = (lat2 - lat1) * (M_PI / 180.0);
    double dlng = (lng2 - lng1) * (M_PI / 180.0);
    double rlat1 = lat1 * (M_PI / 180.0);
    double rlat2 = lat2 * (M_PI / 180.0);

    double sin_half_dlat = sin(dlat * 0.5);
    double sin_half_dlng = sin(dlng * 0.5);

    double a = sin_half_dlat * sin_half_dlat +
               cos(rlat1) * cos(rlat2) * sin_half_dlng * sin_half_dlng;
    if (a < 0.0) a = 0.0;
    if (a > 1.0) a = 1.0;

    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return EARTH_RADIUS_M * c;
}

double graph_max_speed_mps(const Graph *g) {
    if (!g) return 1e-3;
    return g->max_speed_mps < 1e-3 ? 1e-3 : g->max_speed_mps;
}

size_t graph_find_edges_near(const Graph *g, double lat, double lng, double radius_m, int *out_ids, size_t max_out) {
    if (!g || !out_ids || max_out == 0) return 0;
    size_t found = 0;

    for (size_t e = 0; e < g->edge_count && found < max_out; e++) {
        int u = g->edges[e].from;
        int v = g->edges[e].to;
        double mid_lat = (g->nodes[u].lat + g->nodes[v].lat) * 0.5;
        double mid_lng = (g->nodes[u].lng + g->nodes[v].lng) * 0.5;
        double dist = graph_haversine_m(lat, lng, mid_lat, mid_lng);
        if (dist <= radius_m) {
            out_ids[found++] = (int)e;
        }
    }
    return found;
}

int graph_nearest_node(const Graph *g, double lat, double lng, double *dist_m) {
    if (!g || g->node_count == 0) return -1;

    int best_node = -1;
    double best_dist = -1.0;

    for (size_t i = 0; i < g->node_count; i++) {
        double d = graph_haversine_m(lat, lng, g->nodes[i].lat, g->nodes[i].lng);
        if (best_node == -1 || d < best_dist) {
            best_dist = d;
            best_node = (int)i;
        }
    }

    if (dist_m) *dist_m = best_dist;
    return best_node;
}

/* Union-Find for weakly connected components */
static int uf_find(int *parent, int i) {
    while (parent[i] >= 0) {
        if (parent[parent[i]] >= 0) {
            parent[i] = parent[parent[i]];
        }
        i = parent[i];
    }
    return i;
}

static void uf_union(int *parent, int i, int j) {
    int root_i = uf_find(parent, i);
    int root_j = uf_find(parent, j);
    if (root_i != root_j) {
        /* parent[root] stores -size */
        if (parent[root_i] <= parent[root_j]) {
            parent[root_i] += parent[root_j];
            parent[root_j] = root_i;
        } else {
            parent[root_j] += parent[root_i];
            parent[root_i] = root_j;
        }
    }
}

Graph *graph_extract_largest_component(const Graph *g) {
    if (!g || g->node_count == 0) return NULL;

    size_t n = g->node_count;
    int *parent = (int *)malloc(n * sizeof(int));
    if (!parent) return NULL;
    for (size_t i = 0; i < n; i++) {
        parent[i] = -1;
    }

    for (size_t e = 0; e < g->edge_count; e++) {
        uf_union(parent, g->edges[e].from, g->edges[e].to);
    }

    /* Find largest component. Tie breaker: component containing smallest node index */
    int best_root = -1;
    int max_size = -1;
    int min_node_in_best = (int)n + 1;

    /* To track the minimum node in each component */
    int *comp_min_node = (int *)malloc(n * sizeof(int));
    if (!comp_min_node) {
        free(parent);
        return NULL;
    }
    for (size_t i = 0; i < n; i++) comp_min_node[i] = -1;

    for (size_t i = 0; i < n; i++) {
        int r = uf_find(parent, (int)i);
        if (comp_min_node[r] == -1 || (int)i < comp_min_node[r]) {
            comp_min_node[r] = (int)i;
        }
    }

    for (size_t i = 0; i < n; i++) {
        if (parent[i] < 0) {
            int sz = -parent[i];
            int min_node = comp_min_node[i];
            if (sz > max_size || (sz == max_size && min_node < min_node_in_best)) {
                max_size = sz;
                best_root = (int)i;
                min_node_in_best = min_node;
            }
        }
    }
    free(comp_min_node);

    Graph *sub = graph_create();
    if (!sub) {
        free(parent);
        return NULL;
    }

    int *new_index = (int *)malloc(n * sizeof(int));
    if (!new_index) {
        free(parent);
        graph_destroy(sub);
        return NULL;
    }
    for (size_t i = 0; i < n; i++) new_index[i] = -1;

    /* Add nodes in ascending original index order */
    for (size_t i = 0; i < n; i++) {
        if (uf_find(parent, (int)i) == best_root) {
            int idx = graph_add_node(sub, g->nodes[i].osm_id, g->nodes[i].lat, g->nodes[i].lng,
                                     g->nodes[i].name, g->nodes[i].is_stop);
            if (idx < 0) {
                free(parent);
                free(new_index);
                graph_destroy(sub);
                return NULL;
            }
            new_index[i] = idx;
        }
    }
    free(parent);

    /* Remap and add edges */
    for (size_t e = 0; e < g->edge_count; e++) {
        int u = g->edges[e].from;
        int v = g->edges[e].to;
        if (new_index[u] >= 0 && new_index[v] >= 0) {
            int eid = graph_add_edge(sub, new_index[u], new_index[v],
                                     g->edges[e].length_m, g->edges[e].base_time_s,
                                     g->edge_meta[e].road_name, g->edge_meta[e].road_class);
            if (eid < 0) {
                free(new_index);
                graph_destroy(sub);
                return NULL;
            }
        }
    }
    free(new_index);

    graph_reset_traffic(sub);
    return sub;
}

Status graph_save(const Graph *g, const char *path) {
    if (!g || !path) return STATUS_ERR_NULL;

    char tmp_path[512];
    int n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(tmp_path)) return STATUS_ERR_INVALID;

    FILE *f = fopen(tmp_path, "wb");
    if (!f) return STATUS_ERR_INVALID;

    if (fprintf(f, "TRANSPORT_GRAPH v1\nN %zu\n", g->node_count) < 0) {
        fclose(f);
        remove(tmp_path);
        return STATUS_ERR_INVALID;
    }

    for (size_t i = 0; i < g->node_count; i++) {
        char clean_name[64];
        strncpy(clean_name, g->nodes[i].name, sizeof(clean_name) - 1);
        clean_name[sizeof(clean_name) - 1] = '\0';
        for (char *p = clean_name; *p; p++) {
            if (*p == '\t' || *p == '\n' || *p == '\r') *p = ' ';
        }

        if (fprintf(f, "%lld\t%.9g\t%.9g\t%d\t%s\n",
                    (long long)g->nodes[i].osm_id,
                    g->nodes[i].lat,
                    g->nodes[i].lng,
                    g->nodes[i].is_stop,
                    clean_name) < 0) {
            fclose(f);
            remove(tmp_path);
            return STATUS_ERR_INVALID;
        }
    }

    if (fprintf(f, "E %zu\n", g->edge_count) < 0) {
        fclose(f);
        remove(tmp_path);
        return STATUS_ERR_INVALID;
    }

    for (size_t i = 0; i < g->edge_count; i++) {
        char clean_road[48];
        strncpy(clean_road, g->edge_meta[i].road_name, sizeof(clean_road) - 1);
        clean_road[sizeof(clean_road) - 1] = '\0';
        for (char *p = clean_road; *p; p++) {
            if (*p == '\t' || *p == '\n' || *p == '\r') *p = ' ';
        }

        if (fprintf(f, "%d\t%d\t%.9g\t%.9g\t%d\t%s\n",
                    g->edges[i].from,
                    g->edges[i].to,
                    g->edges[i].length_m,
                    g->edges[i].base_time_s,
                    (int)g->edge_meta[i].road_class,
                    clean_road) < 0) {
            fclose(f);
            remove(tmp_path);
            return STATUS_ERR_INVALID;
        }
    }

    fclose(f);
    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        return STATUS_ERR_INVALID;
    }

    return STATUS_OK;
}

Status graph_load(Graph **out, const char *path) {
    if (!out || !path) return STATUS_ERR_NULL;
    *out = NULL;

    FILE *f = fopen(path, "rb");
    if (!f) return STATUS_ERR_NOT_FOUND;

    char line[4096];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return STATUS_ERR_INVALID;
    }

    /* Strip trailing \r\n */
    line[strcspn(line, "\r\n")] = '\0';
    if (strcmp(line, "TRANSPORT_GRAPH v1") != 0) {
        fclose(f);
        return STATUS_ERR_INVALID;
    }

    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return STATUS_ERR_INVALID;
    }
    line[strcspn(line, "\r\n")] = '\0';
    if (strncmp(line, "N ", 2) != 0) {
        fclose(f);
        return STATUS_ERR_INVALID;
    }

    char *endptr = NULL;
    long long n_nodes_ll = strtoll(line + 2, &endptr, 10);
    if (endptr == line + 2 || *endptr != '\0' || n_nodes_ll < 0) {
        fclose(f);
        return STATUS_ERR_INVALID;
    }
    size_t n_nodes = (size_t)n_nodes_ll;

    Graph *g = graph_create();
    if (!g) {
        fclose(f);
        return STATUS_ERR_NOMEM;
    }

    for (size_t i = 0; i < n_nodes; i++) {
        if (!fgets(line, sizeof(line), f)) {
            graph_destroy(g);
            fclose(f);
            return STATUS_ERR_INVALID;
        }
        line[strcspn(line, "\r\n")] = '\0';

        char *token = line;
        char *tab1 = strchr(token, '\t');
        if (!tab1) { graph_destroy(g); fclose(f); return STATUS_ERR_INVALID; }
        *tab1 = '\0';
        long long osm_id_ll = strtoll(token, &endptr, 10);
        if (*endptr != '\0') { graph_destroy(g); fclose(f); return STATUS_ERR_INVALID; }

        token = tab1 + 1;
        char *tab2 = strchr(token, '\t');
        if (!tab2) { graph_destroy(g); fclose(f); return STATUS_ERR_INVALID; }
        *tab2 = '\0';
        double lat = strtod(token, &endptr);
        if (*endptr != '\0') { graph_destroy(g); fclose(f); return STATUS_ERR_INVALID; }

        token = tab2 + 1;
        char *tab3 = strchr(token, '\t');
        if (!tab3) { graph_destroy(g); fclose(f); return STATUS_ERR_INVALID; }
        *tab3 = '\0';
        double lng = strtod(token, &endptr);
        if (*endptr != '\0') { graph_destroy(g); fclose(f); return STATUS_ERR_INVALID; }

        token = tab3 + 1;
        char *tab4 = strchr(token, '\t');
        if (!tab4) { graph_destroy(g); fclose(f); return STATUS_ERR_INVALID; }
        *tab4 = '\0';
        long is_stop_l = strtol(token, &endptr, 10);
        if (*endptr != '\0') { graph_destroy(g); fclose(f); return STATUS_ERR_INVALID; }

        const char *name = tab4 + 1;

        int added_idx = graph_add_node(g, (int64_t)osm_id_ll, lat, lng, name, (int)is_stop_l);
        if (added_idx < 0 || (size_t)added_idx != i) {
            graph_destroy(g);
            fclose(f);
            return STATUS_ERR_INVALID;
        }
    }

    if (!fgets(line, sizeof(line), f)) {
        graph_destroy(g);
        fclose(f);
        return STATUS_ERR_INVALID;
    }
    line[strcspn(line, "\r\n")] = '\0';
    if (strncmp(line, "E ", 2) != 0) {
        graph_destroy(g);
        fclose(f);
        return STATUS_ERR_INVALID;
    }

    long long n_edges_ll = strtoll(line + 2, &endptr, 10);
    if (endptr == line + 2 || *endptr != '\0' || n_edges_ll < 0) {
        graph_destroy(g);
        fclose(f);
        return STATUS_ERR_INVALID;
    }
    size_t n_edges = (size_t)n_edges_ll;

    for (size_t i = 0; i < n_edges; i++) {
        if (!fgets(line, sizeof(line), f)) {
            graph_destroy(g);
            fclose(f);
            return STATUS_ERR_INVALID;
        }
        line[strcspn(line, "\r\n")] = '\0';

        char *token = line;
        char *tab1 = strchr(token, '\t');
        if (!tab1) { graph_destroy(g); fclose(f); return STATUS_ERR_INVALID; }
        *tab1 = '\0';
        long from_l = strtol(token, &endptr, 10);
        if (*endptr != '\0') { graph_destroy(g); fclose(f); return STATUS_ERR_INVALID; }

        token = tab1 + 1;
        char *tab2 = strchr(token, '\t');
        if (!tab2) { graph_destroy(g); fclose(f); return STATUS_ERR_INVALID; }
        *tab2 = '\0';
        long to_l = strtol(token, &endptr, 10);
        if (*endptr != '\0') { graph_destroy(g); fclose(f); return STATUS_ERR_INVALID; }

        token = tab2 + 1;
        char *tab3 = strchr(token, '\t');
        if (!tab3) { graph_destroy(g); fclose(f); return STATUS_ERR_INVALID; }
        *tab3 = '\0';
        double length_m = strtod(token, &endptr);
        if (*endptr != '\0') { graph_destroy(g); fclose(f); return STATUS_ERR_INVALID; }

        token = tab3 + 1;
        char *tab4 = strchr(token, '\t');
        if (!tab4) { graph_destroy(g); fclose(f); return STATUS_ERR_INVALID; }
        *tab4 = '\0';
        double base_time_s = strtod(token, &endptr);
        if (*endptr != '\0') { graph_destroy(g); fclose(f); return STATUS_ERR_INVALID; }

        token = tab4 + 1;
        char *tab5 = strchr(token, '\t');
        if (!tab5) { graph_destroy(g); fclose(f); return STATUS_ERR_INVALID; }
        *tab5 = '\0';
        long cls_l = strtol(token, &endptr, 10);
        if (*endptr != '\0') { graph_destroy(g); fclose(f); return STATUS_ERR_INVALID; }

        const char *road_name = tab5 + 1;

        int eid = graph_add_edge(g, (int)from_l, (int)to_l, length_m, base_time_s, road_name, (RoadClass)cls_l);
        if (eid < 0 || (size_t)eid != i) {
            graph_destroy(g);
            fclose(f);
            return STATUS_ERR_INVALID;
        }
    }

    /* Check if there are unexpected extra lines */
    if (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] != '\0') {
            graph_destroy(g);
            fclose(f);
            return STATUS_ERR_INVALID;
        }
    }

    fclose(f);
    *out = g;
    return STATUS_OK;
}

Status graph_build_synthetic_grid(Graph **out, int rows, int cols, double lat0, double lng0, double spacing_m, uint64_t seed) {
    if (!out) return STATUS_ERR_NULL;
    *out = NULL;
    if (rows < 2 || rows > 400 || cols < 2 || cols > 400 || spacing_m <= 0.0) {
        return STATUS_ERR_INVALID;
    }

    Graph *g = graph_create();
    if (!g) return STATUS_ERR_NOMEM;

    uint64_t rng = seed;
    /* Degree approximation: 1 deg lat ~ 111195 m */
    double dlat_per_m = 1.0 / 111195.0;
    double dlng_per_m = 1.0 / (111195.0 * cos(lat0 * (M_PI / 180.0)));

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int64_t osm_id = 1 + (int64_t)r * cols + c;
            double lat = lat0 - (double)r * spacing_m * dlat_per_m;
            double lng = lng0 + (double)c * spacing_m * dlng_per_m;
            char name[64];
            snprintf(name, sizeof(name), "Grid Node (%d,%d)", r, c);
            int idx = graph_add_node(g, osm_id, lat, lng, name, 0);
            if (idx < 0) {
                graph_destroy(g);
                return STATUS_ERR_INVALID;
            }
        }
    }

    double speeds[3] = { 30.0 / 3.6, 40.0 / 3.6, 50.0 / 3.6 };

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int u = r * cols + c;

            /* Right edge */
            if (c + 1 < cols) {
                int v = r * cols + (c + 1);
                double jitter = splitmix64_double(&rng) * 0.15;
                double len = spacing_m * (1.0 + jitter);
                int sp_idx = (int)(splitmix64(&rng) % 3);
                double speed_mps = speeds[sp_idx];
                double time_s = len / speed_mps;

                RoadClass cls = ROAD_TERTIARY;
                char road_name[48];
                if (r % 5 == 0) {
                    cls = ROAD_PRIMARY;
                    snprintf(road_name, sizeof(road_name), "Grid R%d", r);
                } else {
                    cls = ROAD_SECONDARY;
                    snprintf(road_name, sizeof(road_name), "Grid Street R%d", r);
                }

                if (graph_add_bidirectional(g, u, v, len, time_s, road_name, cls, NULL, NULL) != STATUS_OK) {
                    graph_destroy(g);
                    return STATUS_ERR_INVALID;
                }
            }

            /* Down edge */
            if (r + 1 < rows) {
                int v = (r + 1) * cols + c;
                double jitter = splitmix64_double(&rng) * 0.15;
                double len = spacing_m * (1.0 + jitter);
                int sp_idx = (int)(splitmix64(&rng) % 3);
                double speed_mps = speeds[sp_idx];
                double time_s = len / speed_mps;

                RoadClass cls = ROAD_TERTIARY;
                char road_name[48];
                if (c % 5 == 0) {
                    cls = ROAD_PRIMARY;
                    snprintf(road_name, sizeof(road_name), "Grid C%d", c);
                } else {
                    cls = ROAD_SECONDARY;
                    snprintf(road_name, sizeof(road_name), "Grid Street C%d", c);
                }

                if (graph_add_bidirectional(g, u, v, len, time_s, road_name, cls, NULL, NULL) != STATUS_OK) {
                    graph_destroy(g);
                    return STATUS_ERR_INVALID;
                }
            }
        }
    }

    *out = g;
    return STATUS_OK;
}

