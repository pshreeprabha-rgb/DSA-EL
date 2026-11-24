#include "graph_scc.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

typedef struct DfsFrame {
    int u;
    size_t edge_idx;
} DfsFrame;

Status graph_scc_label(const Graph *g, int *labels, size_t *num_components) {
    if (!g || !labels || !num_components) {
        return STATUS_ERR_NULL;
    }

    size_t n = graph_node_count(g);
    if (n == 0) {
        *num_components = 0;
        return STATUS_ERR_INVALID;
    }

    int *dfn = (int *)malloc(n * sizeof(int));
    int *low = (int *)malloc(n * sizeof(int));
    int *in_tarjan = (int *)calloc(n, sizeof(int));
    int *tarjan_stack = (int *)malloc(n * sizeof(int));
    DfsFrame *dfs_stack = (DfsFrame *)malloc(n * sizeof(DfsFrame));

    if (!dfn || !low || !in_tarjan || !tarjan_stack || !dfs_stack) {
        free(dfn);
        free(low);
        free(in_tarjan);
        free(tarjan_stack);
        free(dfs_stack);
        return STATUS_ERR_NOMEM;
    }

    for (size_t i = 0; i < n; i++) {
        dfn[i] = -1;
        low[i] = -1;
    }

    int current_index = 0;
    size_t tarjan_top = 0;
    size_t dfs_top = 0;
    size_t comp_count = 0;

    for (size_t root = 0; root < n; root++) {
        if (dfn[root] != -1) {
            continue;
        }

        dfn[root] = low[root] = current_index++;
        tarjan_stack[tarjan_top++] = (int)root;
        in_tarjan[root] = 1;

        dfs_stack[dfs_top].u = (int)root;
        dfs_stack[dfs_top].edge_idx = 0;
        dfs_top++;

        while (dfs_top > 0) {
            DfsFrame *top = &dfs_stack[dfs_top - 1];
            int u = top->u;
            size_t out_deg = (size_t)graph_out_degree(g, u);

            if (top->edge_idx < out_deg) {
                int eid = graph_out_edge(g, u, top->edge_idx);
                top->edge_idx++;

                const Edge *e = graph_edge(g, eid);
                if (!e) {
                    continue;
                }
                /* NOTE: All edges are considered regardless of blocked flag */
                int v = e->to;
                if (v < 0 || (size_t)v >= n) {
                    continue;
                }

                if (dfn[v] == -1) {
                    /* Unvisited tree edge: push onto DFS stack */
                    dfn[v] = low[v] = current_index++;
                    tarjan_stack[tarjan_top++] = v;
                    in_tarjan[v] = 1;

                    dfs_stack[dfs_top].u = v;
                    dfs_stack[dfs_top].edge_idx = 0;
                    dfs_top++;
                } else if (in_tarjan[v]) {
                    /* Back-edge / cross-edge to node in current SCC */
                    if (dfn[v] < low[u]) {
                        low[u] = dfn[v];
                    }
                }
            } else {
                /* Finished exploring outgoing edges of u */
                if (low[u] == dfn[u]) {
                    /* Root of an SCC */
                    int w = -1;
                    do {
                        w = tarjan_stack[--tarjan_top];
                        in_tarjan[w] = 0;
                        labels[w] = (int)comp_count;
                    } while (w != u);
                    comp_count++;
                }

                dfs_top--;
                if (dfs_top > 0) {
                    int parent = dfs_stack[dfs_top - 1].u;
                    if (low[u] < low[parent]) {
                        low[parent] = low[u];
                    }
                }
            }
        }
    }

    *num_components = comp_count;

    free(dfn);
    free(low);
    free(in_tarjan);
    free(tarjan_stack);
    free(dfs_stack);

    return STATUS_OK;
}

Status graph_largest_scc(const Graph *g, int **nodes_out, size_t *count) {
    if (!g || !nodes_out || !count) {
        return STATUS_ERR_NULL;
    }
    *nodes_out = NULL;
    *count = 0;

    size_t n = graph_node_count(g);
    if (n == 0) {
        return STATUS_ERR_INVALID;
    }

    int *labels = (int *)malloc(n * sizeof(int));
    if (!labels) {
        return STATUS_ERR_NOMEM;
    }

    size_t num_components = 0;
    Status st = graph_scc_label(g, labels, &num_components);
    if (st != STATUS_OK || num_components == 0) {
        free(labels);
        return (st == STATUS_OK) ? STATUS_ERR_INVALID : st;
    }

    size_t *comp_sizes = (size_t *)calloc(num_components, sizeof(size_t));
    int *comp_min_node = (int *)malloc(num_components * sizeof(int));
    if (!comp_sizes || !comp_min_node) {
        free(labels);
        free(comp_sizes);
        free(comp_min_node);
        return STATUS_ERR_NOMEM;
    }

    for (size_t c = 0; c < num_components; c++) {
        comp_min_node[c] = INT_MAX;
    }

    for (size_t i = 0; i < n; i++) {
        int c = labels[i];
        if (c >= 0 && (size_t)c < num_components) {
            comp_sizes[c]++;
            if ((int)i < comp_min_node[c]) {
                comp_min_node[c] = (int)i;
            }
        }
    }

    size_t best_c = 0;
    size_t max_size = comp_sizes[0];

    for (size_t c = 1; c < num_components; c++) {
        if (comp_sizes[c] > max_size) {
            max_size = comp_sizes[c];
            best_c = c;
        } else if (comp_sizes[c] == max_size) {
            /* Tie breaker: component containing smallest node index */
            if (comp_min_node[c] < comp_min_node[best_c]) {
                best_c = c;
            }
        }
    }

    int *result_nodes = (int *)malloc(max_size * sizeof(int));
    if (!result_nodes) {
        free(labels);
        free(comp_sizes);
        free(comp_min_node);
        return STATUS_ERR_NOMEM;
    }

    size_t out_idx = 0;
    for (size_t i = 0; i < n; i++) {
        if (labels[i] == (int)best_c) {
            result_nodes[out_idx++] = (int)i;
        }
    }

    free(labels);
    free(comp_sizes);
    free(comp_min_node);

    *nodes_out = result_nodes;
    *count = max_size;

    return STATUS_OK;
}
