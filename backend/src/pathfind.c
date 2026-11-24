#define _POSIX_C_SOURCE 200809L

#include "pathfind.h"
#include "heap.h"
#include "hashtable.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct SearchNode {
    int node;
    double g;
    double h;
    double f;
    int parent_node;
    int parent_edge;
    size_t heap_index;
    int in_open;
} SearchNode;

static void search_node_free(void *ptr) {
    free(ptr);
}

static void search_node_index_fn(void *item, size_t index) {
    SearchNode *sn = (SearchNode *)item;
    if (sn) {
        sn->heap_index = index;
    }
}

/*
 * Comparator for Heap:
 * Returns < 0 if a has higher priority than b (min-heap).
 * 1. Smaller f
 * 2. Smaller h (larger g)
 * 3. Smaller node index (determinism)
 */
static int search_node_cmp(const void *va, const void *vb) {
    const SearchNode *a = (const SearchNode *)va;
    const SearchNode *b = (const SearchNode *)vb;

    if (fabs(a->f - b->f) > 1e-12) {
        return (a->f < b->f) ? -1 : 1;
    }
    if (fabs(a->h - b->h) > 1e-12) {
        return (a->h < b->h) ? -1 : 1;
    }
    if (a->node != b->node) {
        return (a->node < b->node) ? -1 : 1;
    }
    return 0;
}

void path_result_init(PathResult *p) {
    if (!p) return;
    p->found = 0;
    p->total_time_s = 0.0;
    p->total_length_m = 0.0;
    p->nodes_explored = 0;
    p->nodes_generated = 0;
    p->node_count = 0;
    p->nodes = NULL;
    p->edge_count = 0;
    p->edges = NULL;
}

void path_result_free(PathResult *p) {
    if (!p) return;
    free(p->nodes);
    free(p->edges);
    path_result_init(p);
}

Status path_result_copy(PathResult *dst, const PathResult *src) {
    if (!dst || !src) return STATUS_ERR_NULL;
    path_result_free(dst);

    dst->found = src->found;
    dst->total_time_s = src->total_time_s;
    dst->total_length_m = src->total_length_m;
    dst->nodes_explored = src->nodes_explored;
    dst->nodes_generated = src->nodes_generated;
    dst->node_count = src->node_count;
    dst->edge_count = src->edge_count;

    if (src->node_count > 0 && src->nodes != NULL) {
        dst->nodes = (int *)malloc(src->node_count * sizeof(int));
        if (!dst->nodes) {
            path_result_free(dst);
            return STATUS_ERR_NOMEM;
        }
        memcpy(dst->nodes, src->nodes, src->node_count * sizeof(int));
    } else {
        dst->nodes = NULL;
    }

    if (src->edge_count > 0 && src->edges != NULL) {
        dst->edges = (int *)malloc(src->edge_count * sizeof(int));
        if (!dst->edges) {
            path_result_free(dst);
            return STATUS_ERR_NOMEM;
        }
        memcpy(dst->edges, src->edges, src->edge_count * sizeof(int));
    } else {
        dst->edges = NULL;
    }

    return STATUS_OK;
}

Status find_path(const Graph *g, int src, int dst, SearchAlgo algo, CostMode mode, PathResult *out) {
    if (!out) return STATUS_ERR_NULL;
    path_result_free(out);

    if (!g) return STATUS_ERR_NULL;
    size_t num_nodes = graph_node_count(g);
    if (src < 0 || (size_t)src >= num_nodes || dst < 0 || (size_t)dst >= num_nodes) {
        return STATUS_ERR_INVALID;
    }

    /*
     * Because the heuristic h(n) = haversine(n, goal) / max_speed is consistent
     * (the triangle inequality holds, and speed is strictly bounded by max_speed,
     * with traffic_factor >= 1.0), the optimal distance to a node is found the
     * first time it is popped from the open list. Hence, closed nodes never need
     * to be reopened.
     */
    HashTable *closed = ht_create(HT_KEY_INT, NULL);
    HashTable *records = ht_create(HT_KEY_INT, search_node_free);
    Heap *open_list = heap_create(search_node_cmp, NULL, search_node_index_fn);

    if (!closed || !records || !open_list) {
        ht_destroy(closed);
        ht_destroy(records);
        heap_destroy(open_list);
        return STATUS_ERR_NOMEM;
    }

    double max_speed = graph_max_speed_mps(g);
    const GraphNode *dst_node = graph_node(g, dst);

    SearchNode *start_rec = (SearchNode *)calloc(1, sizeof(SearchNode));
    if (!start_rec) {
        ht_destroy(closed);
        ht_destroy(records);
        heap_destroy(open_list);
        return STATUS_ERR_NOMEM;
    }

    start_rec->node = src;
    start_rec->g = 0.0;
    if (algo == SEARCH_ASTAR && dst_node != NULL) {
        const GraphNode *s_node = graph_node(g, src);
        start_rec->h = graph_haversine_m(s_node->lat, s_node->lng, dst_node->lat, dst_node->lng) / max_speed;
    } else {
        start_rec->h = 0.0;
    }
    start_rec->f = start_rec->g + start_rec->h;
    start_rec->parent_node = -1;
    start_rec->parent_edge = -1;
    start_rec->in_open = 1;

    ht_put_int(records, src, start_rec);
    heap_push(open_list, start_rec);

    size_t nodes_explored = 0;
    size_t nodes_generated = 1;
    int goal_reached = 0;

    while (heap_peek(open_list) != NULL) {
        SearchNode *curr = (SearchNode *)heap_pop(open_list);
        curr->in_open = 0;
        nodes_explored++;
        ht_put_int(closed, curr->node, NULL);

        if (curr->node == dst) {
            goal_reached = 1;
            break;
        }

        size_t deg = graph_out_degree(g, curr->node);
        for (size_t k = 0; k < deg; k++) {
            int eid = graph_out_edge(g, curr->node, k);
            if (eid < 0) continue;
            const Edge *edge = graph_edge(g, eid);
            if (!edge || edge->blocked) continue;

            int neighbor = edge->to;
            if (ht_contains_int(closed, neighbor)) {
                /* Consistently heuristic: closed nodes are never reopened */
                continue;
            }

            double edge_cost = graph_edge_cost_s(g, eid, mode);
            double tentative_g = curr->g + edge_cost;

            SearchNode *neighbor_rec = (SearchNode *)ht_get_int(records, neighbor);
            if (!neighbor_rec) {
                neighbor_rec = (SearchNode *)calloc(1, sizeof(SearchNode));
                if (!neighbor_rec) {
                    ht_destroy(closed);
                    ht_destroy(records);
                    heap_destroy(open_list);
                    return STATUS_ERR_NOMEM;
                }
                neighbor_rec->node = neighbor;
                neighbor_rec->g = tentative_g;
                if (algo == SEARCH_ASTAR && dst_node != NULL) {
                    const GraphNode *n_node = graph_node(g, neighbor);
                    neighbor_rec->h = graph_haversine_m(n_node->lat, n_node->lng, dst_node->lat, dst_node->lng) / max_speed;
                } else {
                    neighbor_rec->h = 0.0;
                }
                neighbor_rec->f = neighbor_rec->g + neighbor_rec->h;
                neighbor_rec->parent_node = curr->node;
                neighbor_rec->parent_edge = eid;
                neighbor_rec->in_open = 1;

                ht_put_int(records, neighbor, neighbor_rec);
                heap_push(open_list, neighbor_rec);
                nodes_generated++;
            } else if (tentative_g < neighbor_rec->g) {
                neighbor_rec->g = tentative_g;
                neighbor_rec->f = neighbor_rec->g + neighbor_rec->h;
                neighbor_rec->parent_node = curr->node;
                neighbor_rec->parent_edge = eid;
                if (neighbor_rec->in_open) {
                    heap_update(open_list, neighbor_rec->heap_index);
                } else {
                    neighbor_rec->in_open = 1;
                    heap_push(open_list, neighbor_rec);
                }
            }
        }
    }

    out->nodes_explored = nodes_explored;
    out->nodes_generated = nodes_generated;

    if (!goal_reached) {
        out->found = 0;
        ht_destroy(closed);
        ht_destroy(records);
        heap_destroy(open_list);
        return STATUS_OK;
    }

    out->found = 1;

    /* Reconstruct path via parent pointers */
    SearchNode *target = (SearchNode *)ht_get_int(records, dst);
    out->total_time_s = target->g;

    /* Count nodes */
    size_t count = 0;
    SearchNode *crawl = target;
    while (crawl != NULL) {
        count++;
        if (crawl->parent_node == -1) break;
        crawl = (SearchNode *)ht_get_int(records, crawl->parent_node);
    }

    out->node_count = count;
    out->edge_count = (count > 0) ? count - 1 : 0;

    out->nodes = (int *)malloc(out->node_count * sizeof(int));
    if (out->edge_count > 0) {
        out->edges = (int *)malloc(out->edge_count * sizeof(int));
    } else {
        out->edges = NULL;
    }

    if (!out->nodes || (out->edge_count > 0 && !out->edges)) {
        path_result_free(out);
        ht_destroy(closed);
        ht_destroy(records);
        heap_destroy(open_list);
        return STATUS_ERR_NOMEM;
    }

    crawl = target;
    double total_len = 0.0;
    for (size_t i = 0; i < out->node_count; i++) {
        size_t node_idx = out->node_count - 1 - i;
        out->nodes[node_idx] = crawl->node;
        if (crawl->parent_edge != -1 && i < out->edge_count) {
            size_t edge_idx = out->edge_count - 1 - i;
            out->edges[edge_idx] = crawl->parent_edge;
            const Edge *e = graph_edge(g, crawl->parent_edge);
            if (e) total_len += e->length_m;
        }
        if (crawl->parent_node != -1) {
            crawl = (SearchNode *)ht_get_int(records, crawl->parent_node);
        } else {
            crawl = NULL;
        }
    }
    out->total_length_m = total_len;

    ht_destroy(closed);
    ht_destroy(records);
    heap_destroy(open_list);
    return STATUS_OK;
}

Status astar_find_path(const Graph *g, int src, int dst, CostMode mode, PathResult *out) {
    return find_path(g, src, dst, SEARCH_ASTAR, mode, out);
}

Status dijkstra_find_path(const Graph *g, int src, int dst, CostMode mode, PathResult *out) {
    return find_path(g, src, dst, SEARCH_DIJKSTRA, mode, out);
}

double path_time_s(const Graph *g, const PathResult *p, CostMode mode) {
    if (!g || !p || !p->found) return -1.0;
    if (p->node_count == 0) return -1.0;
    if (p->edge_count == 0) return 0.0;
    if (!path_is_valid(g, p)) return -1.0;

    double sum = 0.0;
    for (size_t i = 0; i < p->edge_count; i++) {
        const Edge *e = graph_edge(g, p->edges[i]);
        if (!e || e->blocked) return -1.0;
        sum += graph_edge_cost_s(g, p->edges[i], mode);
    }
    return sum;
}

int path_contains_edge(const PathResult *p, int edge_id) {
    if (!p || !p->edges || edge_id < 0) return 0;
    for (size_t i = 0; i < p->edge_count; i++) {
        if (p->edges[i] == edge_id) return 1;
    }
    return 0;
}

int path_is_valid(const Graph *g, const PathResult *p) {
    if (!g || !p || !p->found) return 0;
    if (p->node_count == 0) return 0;
    if (p->edge_count != p->node_count - 1) return 0;
    if (p->node_count == 1) return (p->edge_count == 0);

    for (size_t i = 0; i < p->edge_count; i++) {
        const Edge *e = graph_edge(g, p->edges[i]);
        if (!e || e->blocked) return 0;
        if (e->from != p->nodes[i] || e->to != p->nodes[i + 1]) {
            return 0;
        }
    }
    return 1;
}

