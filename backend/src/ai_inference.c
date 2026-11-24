#include "ai_inference.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct ForestNode {
    int left;
    int right;
    int feature;
    double threshold;
    double value;
} ForestNode;

typedef struct DecisionTree {
    size_t node_count;
    ForestNode *nodes;
} DecisionTree;

struct ForestModel {
    size_t tree_count;
    size_t total_nodes;
    DecisionTree *trees;
};

static char *read_clean_line(char *buf, size_t buf_size, FILE *f) {
    if (!fgets(buf, (int)buf_size, f)) {
        return NULL;
    }
    size_t len = strlen(buf);
    if (len > 256 || (len > 0 && buf[len - 1] != '\n' && !feof(f))) {
        return NULL; /* line too long */
    }
    while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n')) {
        buf[--len] = '\0';
    }
    return buf;
}

static char *next_tab_field(char **cursor) {
    if (!cursor || !*cursor) return NULL;
    char *start = *cursor;
    char *p = start;
    while (*p && *p != '\t') {
        p++;
    }
    if (*p == '\t') {
        *p = '\0';
        *cursor = p + 1;
    } else {
        *cursor = NULL;
    }
    return start;
}

void forest_destroy(ForestModel *m) {
    if (!m) return;
    if (m->trees) {
        for (size_t t = 0; t < m->tree_count; t++) {
            free(m->trees[t].nodes);
        }
        free(m->trees);
    }
    free(m);
}

Status forest_load(ForestModel **out, const char *path) {
    if (!out || !path) {
        return STATUS_ERR_NULL;
    }
    *out = NULL;

    FILE *f = fopen(path, "rb");
    if (!f) {
        return STATUS_ERR_NOT_FOUND;
    }

    char line[512];
    /* Line 1: RF_MODEL v1 */
    if (!read_clean_line(line, sizeof(line), f) || strcmp(line, "RF_MODEL v1") != 0) {
        fclose(f);
        return STATUS_ERR_INVALID;
    }

    /* Line 2: FEATURES 10 hour weekend holiday rain event type_hub type_office type_market type_residential stop_scale */
    static const char EXPECTED_FEATS[] = "FEATURES 10 hour weekend holiday rain event type_hub type_office type_market type_residential stop_scale";
    if (!read_clean_line(line, sizeof(line), f) || strcmp(line, EXPECTED_FEATS) != 0) {
        fclose(f);
        return STATUS_ERR_INVALID;
    }

    /* Line 3: TREES <T> */
    if (!read_clean_line(line, sizeof(line), f)) {
        fclose(f);
        return STATUS_ERR_INVALID;
    }
    if (strncmp(line, "TREES ", 6) != 0) {
        fclose(f);
        return STATUS_ERR_INVALID;
    }
    char *endptr = NULL;
    long t_count = strtol(line + 6, &endptr, 10);
    if (*endptr != '\0' || t_count < 1 || t_count > 500) {
        fclose(f);
        return STATUS_ERR_INVALID;
    }

    ForestModel *m = (ForestModel *)calloc(1, sizeof(ForestModel));
    if (!m) {
        fclose(f);
        return STATUS_ERR_NOMEM;
    }
    m->tree_count = (size_t)t_count;
    m->trees = (DecisionTree *)calloc(m->tree_count, sizeof(DecisionTree));
    if (!m->trees) {
        free(m);
        fclose(f);
        return STATUS_ERR_NOMEM;
    }

    for (size_t t = 0; t < m->tree_count; t++) {
        /* Tree header: TREE <index> NODES <m> */
        if (!read_clean_line(line, sizeof(line), f)) {
            forest_destroy(m);
            fclose(f);
            return STATUS_ERR_INVALID;
        }
        if (strncmp(line, "TREE ", 5) != 0) {
            forest_destroy(m);
            fclose(f);
            return STATUS_ERR_INVALID;
        }

        char *cursor = line + 5;
        long tree_idx = strtol(cursor, &endptr, 10);
        if (tree_idx != (long)t) {
            forest_destroy(m);
            fclose(f);
            return STATUS_ERR_INVALID;
        }
        cursor = endptr;
        while (*cursor == ' ') cursor++;
        if (strncmp(cursor, "NODES ", 6) != 0) {
            forest_destroy(m);
            fclose(f);
            return STATUS_ERR_INVALID;
        }
        cursor += 6;
        long n_nodes = strtol(cursor, &endptr, 10);
        if (*endptr != '\0' || n_nodes < 1 || n_nodes > 100000) {
            forest_destroy(m);
            fclose(f);
            return STATUS_ERR_INVALID;
        }

        m->trees[t].node_count = (size_t)n_nodes;
        m->trees[t].nodes = (ForestNode *)malloc(m->trees[t].node_count * sizeof(ForestNode));
        if (!m->trees[t].nodes) {
            forest_destroy(m);
            fclose(f);
            return STATUS_ERR_NOMEM;
        }

        for (size_t i = 0; i < m->trees[t].node_count; i++) {
            if (!read_clean_line(line, sizeof(line), f)) {
                forest_destroy(m);
                fclose(f);
                return STATUS_ERR_INVALID;
            }

            char *tok_cursor = line;
            char *tok = next_tab_field(&tok_cursor);
            if (!tok) { forest_destroy(m); fclose(f); return STATUS_ERR_INVALID; }
            long left = strtol(tok, &endptr, 10);
            if (*endptr != '\0') { forest_destroy(m); fclose(f); return STATUS_ERR_INVALID; }

            tok = next_tab_field(&tok_cursor);
            if (!tok) { forest_destroy(m); fclose(f); return STATUS_ERR_INVALID; }
            long right = strtol(tok, &endptr, 10);
            if (*endptr != '\0') { forest_destroy(m); fclose(f); return STATUS_ERR_INVALID; }

            tok = next_tab_field(&tok_cursor);
            if (!tok) { forest_destroy(m); fclose(f); return STATUS_ERR_INVALID; }
            long feat = strtol(tok, &endptr, 10);
            if (*endptr != '\0') { forest_destroy(m); fclose(f); return STATUS_ERR_INVALID; }

            tok = next_tab_field(&tok_cursor);
            if (!tok) { forest_destroy(m); fclose(f); return STATUS_ERR_INVALID; }
            double thresh = strtod(tok, &endptr);
            if (*endptr != '\0' || !isfinite(thresh)) { forest_destroy(m); fclose(f); return STATUS_ERR_INVALID; }

            tok = next_tab_field(&tok_cursor);
            if (!tok) { forest_destroy(m); fclose(f); return STATUS_ERR_INVALID; }
            double val = strtod(tok, &endptr);
            if (*endptr != '\0' || !isfinite(val)) { forest_destroy(m); fclose(f); return STATUS_ERR_INVALID; }

            if (tok_cursor != NULL) { forest_destroy(m); fclose(f); return STATUS_ERR_INVALID; }

            /* Node validity check */
            if (left == -1 && right == -1) {
                /* Leaf node */
                if (feat != -1) {
                    forest_destroy(m);
                    fclose(f);
                    return STATUS_ERR_INVALID;
                }
            } else if (left != -1 && right != -1) {
                /* Internal node */
                if (feat < 0 || feat >= DEMAND_NUM_FEATURES) {
                    forest_destroy(m);
                    fclose(f);
                    return STATUS_ERR_INVALID;
                }
                /* Children must have indices strictly greater than node index and < n_nodes */
                if (left <= (long)i || right <= (long)i || left >= n_nodes || right >= n_nodes) {
                    forest_destroy(m);
                    fclose(f);
                    return STATUS_ERR_INVALID;
                }
            } else {
                /* Single child node is invalid */
                forest_destroy(m);
                fclose(f);
                return STATUS_ERR_INVALID;
            }

            m->trees[t].nodes[i].left = (int)left;
            m->trees[t].nodes[i].right = (int)right;
            m->trees[t].nodes[i].feature = (int)feat;
            m->trees[t].nodes[i].threshold = thresh;
            m->trees[t].nodes[i].value = val;
        }

        m->total_nodes += m->trees[t].node_count;

        /* Tree closing line: END */
        if (!read_clean_line(line, sizeof(line), f) || strcmp(line, "END") != 0) {
            forest_destroy(m);
            fclose(f);
            return STATUS_ERR_INVALID;
        }
    }

    /* Check for trailing garbage */
    while (fgets(line, sizeof(line), f)) {
        size_t l_len = strlen(line);
        while (l_len > 0 && (line[l_len - 1] == '\r' || line[l_len - 1] == '\n' || line[l_len - 1] == ' ')) {
            line[--l_len] = '\0';
        }
        if (l_len > 0) {
            forest_destroy(m);
            fclose(f);
            return STATUS_ERR_INVALID;
        }
    }

    fclose(f);
    *out = m;
    return STATUS_OK;
}

double forest_predict(const ForestModel *m, const double features[DEMAND_NUM_FEATURES]) {
    if (!m || !features || m->tree_count == 0) {
        return NAN;
    }

    for (int i = 0; i < DEMAND_NUM_FEATURES; i++) {
        if (!isfinite(features[i])) {
            return NAN;
        }
    }

    double sum = 0.0;
    for (size_t t = 0; t < m->tree_count; t++) {
        const ForestNode *nodes = m->trees[t].nodes;
        int curr = 0;

        while (nodes[curr].left != -1) {
            int f = nodes[curr].feature;
            float feat_cast = (float)features[f];
            if ((double)feat_cast <= nodes[curr].threshold) {
                curr = nodes[curr].left;
            } else {
                curr = nodes[curr].right;
            }
        }
        sum += nodes[curr].value;
    }

    return sum / (double)m->tree_count;
}

size_t forest_tree_count(const ForestModel *m) {
    return m ? m->tree_count : 0;
}

size_t forest_node_count(const ForestModel *m) {
    return m ? m->total_nodes : 0;
}
