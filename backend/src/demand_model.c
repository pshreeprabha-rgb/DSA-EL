#include "demand_model.h"
#include "graph_scc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Approximate reference centres in central Bangalore (approximate, verify on a map) */
static const double HUB_MAJESTIC_LAT = 12.9767;
static const double HUB_MAJESTIC_LNG = 77.5713;
static const double HUB_SHIVAJINAGAR_LAT = 12.9857;
static const double HUB_SHIVAJINAGAR_LNG = 77.6057;

static const double OFFICE_MGROAD_LAT = 12.9756;
static const double OFFICE_MGROAD_LNG = 77.6068;
static const double OFFICE_INDIRANAGAR_LAT = 12.9784;
static const double OFFICE_INDIRANAGAR_LNG = 77.6408;
static const double OFFICE_KORAMANGALA_LAT = 12.9352;
static const double OFFICE_KORAMANGALA_LNG = 77.6245;

static inline double gaussian_kernel(double t, double mu, double s) {
    double diff = (t - mu) / s;
    return exp(-0.5 * diff * diff);
}

double demand_expected(StopType type, double scale, int hour, DayType day_type, int rain, int event) {
    if (hour < 0 || hour > 23 || scale <= 0.0) {
        return -1.0;
    }
    if ((int)type < 0 || (int)type > 3) {
        return -1.0;
    }
    if ((int)day_type < 0 || (int)day_type > 2) {
        return -1.0;
    }

    double base = 0.0;
    double flr = 0.0;
    double a1 = 0.0, mu1 = 0.0, s1 = 1.0;
    double a2 = 0.0, mu2 = 0.0, s2 = 1.0;
    double a3 = 0.0, mu3 = 0.0, s3 = 1.0;
    double we_mult = 1.0;
    double hol_mult = 1.0;

    switch (type) {
        case STOP_HUB:
            base = 60.0; flr = 0.15;
            a1 = 0.85; mu1 = 8.5;  s1 = 1.4;
            a2 = 0.90; mu2 = 18.0; s2 = 1.6;
            a3 = 0.35; mu3 = 13.0; s3 = 3.0;
            we_mult = 0.80; hol_mult = 0.70;
            break;
        case STOP_OFFICE:
            base = 28.0; flr = 0.05;
            a1 = 1.00; mu1 = 9.0;  s1 = 1.0;
            a2 = 0.80; mu2 = 18.5; s2 = 1.2;
            a3 = 0.20; mu3 = 13.0; s3 = 2.0;
            we_mult = 0.30; hol_mult = 0.20;
            break;
        case STOP_MARKET:
            base = 24.0; flr = 0.05;
            a1 = 0.30; mu1 = 9.5;  s1 = 1.5;
            a2 = 0.55; mu2 = 17.5; s2 = 1.8;
            a3 = 0.80; mu3 = 12.5; s3 = 3.0;
            we_mult = 1.25; hol_mult = 1.10;
            break;
        case STOP_RESIDENTIAL:
            base = 14.0; flr = 0.05;
            a1 = 1.00; mu1 = 8.0;  s1 = 1.2;
            a2 = 0.55; mu2 = 19.0; s2 = 1.5;
            a3 = 0.10; mu3 = 13.0; s3 = 3.0;
            we_mult = 0.70; hol_mult = 0.60;
            break;
        default:
            return -1.0;
    }

    double t = (double)hour + 0.5;
    double profile = flr + a1 * gaussian_kernel(t, mu1, s1)
                         + a2 * gaussian_kernel(t, mu2, s2)
                         + a3 * gaussian_kernel(t, mu3, s3);

    double daymult = 1.0;
    if (day_type == DAY_WEEKEND) {
        daymult = we_mult;
    } else if (day_type == DAY_HOLIDAY) {
        daymult = hol_mult;
    }

    double rain_mult = (rain != 0) ? 1.20 : 1.0;
    double event_mult = (event != 0) ? 2.5 : 1.0;

    double lambda = base * scale * profile * daymult * rain_mult * event_mult;
    if (lambda < 0.0) {
        lambda = 0.0;
    }
    return lambda;
}

void demand_features(StopType type, double scale, int hour, DayType day_type, int rain, int event, double out[DEMAND_NUM_FEATURES]) {
    if (!out) return;
    out[0] = (double)hour;
    out[1] = (day_type == DAY_WEEKEND) ? 1.0 : 0.0;
    out[2] = (day_type == DAY_HOLIDAY) ? 1.0 : 0.0;
    out[3] = (rain != 0) ? 1.0 : 0.0;
    out[4] = (event != 0) ? 1.0 : 0.0;
    out[5] = (type == STOP_HUB) ? 1.0 : 0.0;
    out[6] = (type == STOP_OFFICE) ? 1.0 : 0.0;
    out[7] = (type == STOP_MARKET) ? 1.0 : 0.0;
    out[8] = (type == STOP_RESIDENTIAL) ? 1.0 : 0.0;
    out[9] = scale;
}

void demand_calendar(uint64_t seed, int day, DayType *day_type, int *rain, int *event_day) {
    if (day_type) {
        int rem = ((day % 7) + 7) % 7;
        if (rem == 5 || rem == 6) {
            *day_type = DAY_WEEKEND;
        } else {
            double uh = demand_unit(demand_hash(seed, (uint64_t)day, 0, 101));
            if (uh < 0.05) {
                *day_type = DAY_HOLIDAY;
            } else {
                *day_type = DAY_WEEKDAY;
            }
        }
    }
    if (rain) {
        double ur = demand_unit(demand_hash(seed, (uint64_t)day, 0, 102));
        *rain = (ur < 0.20) ? 1 : 0;
    }
    if (event_day) {
        double ue = demand_unit(demand_hash(seed, (uint64_t)day, 0, 103));
        *event_day = (ue < 0.08) ? 1 : 0;
    }
}

size_t demand_event_stops(uint64_t seed, int day, size_t n_stops, int out[3]) {
    if (!out || n_stops == 0) return 0;
    if (n_stops < 3) {
        for (size_t i = 0; i < n_stops; i++) {
            out[i] = (int)i;
        }
        return n_stops;
    }

    for (size_t k = 0; k < 3; k++) {
        uint64_t retry = 0;
        int idx = 0;
        while (1) {
            uint64_t h = demand_hash(seed, (uint64_t)day, 1000 + k + 10 * retry, 104);
            idx = (int)(h % n_stops);
            int dup = 0;
            for (size_t prev = 0; prev < k; prev++) {
                if (out[prev] == idx) {
                    dup = 1;
                    break;
                }
            }
            if (!dup) break;
            retry++;
        }
        out[k] = idx;
    }
    return 3;
}

double demand_stop_scale(uint64_t seed, int64_t osm_id) {
    double u = demand_unit(demand_hash(seed, (uint64_t)osm_id, 0, 555));
    return round((0.6 + 1.2 * u) * 64.0) / 64.0;
}

Status demand_stops_from_graph(const Graph *g, size_t max_stops, uint64_t seed, DemandStop **out, size_t *count) {
    if (!g || !out || !count) {
        return STATUS_ERR_NULL;
    }
    *out = NULL;
    *count = 0;

    if (max_stops == 0) {
        return STATUS_ERR_INVALID;
    }

    int *scc_nodes = NULL;
    size_t scc_count = 0;
    Status st = graph_largest_scc(g, &scc_nodes, &scc_count);
    if (st != STATUS_OK || scc_count == 0) {
        free(scc_nodes);
        return (st == STATUS_OK) ? STATUS_ERR_INVALID : st;
    }

    /* Collect candidates: nodes in largest SCC that have is_stop == 1 */
    int *candidates = (int *)malloc(scc_count * sizeof(int));
    if (!candidates) {
        free(scc_nodes);
        return STATUS_ERR_NOMEM;
    }

    size_t cand_count = 0;
    for (size_t i = 0; i < scc_count; i++) {
        int u = scc_nodes[i];
        const GraphNode *gn = graph_node(g, u);
        if (gn && gn->is_stop) {
            candidates[cand_count++] = u;
        }
    }
    free(scc_nodes);

    if (cand_count == 0) {
        free(candidates);
        return STATUS_ERR_INVALID;
    }

    size_t selected_count = (cand_count > max_stops) ? max_stops : cand_count;
    DemandStop *stops = (DemandStop *)calloc(selected_count, sizeof(DemandStop));
    if (!stops) {
        free(candidates);
        return STATUS_ERR_NOMEM;
    }

    for (size_t k = 0; k < selected_count; k++) {
        size_t cand_idx = (cand_count > max_stops) ? ((k * cand_count) / max_stops) : k;
        int node_idx = candidates[cand_idx];
        const GraphNode *gn = graph_node(g, node_idx);

        stops[k].node = node_idx;
        stops[k].osm_id = gn ? gn->osm_id : 0;
        stops[k].lat = gn ? gn->lat : 0.0;
        stops[k].lng = gn ? gn->lng : 0.0;
        if (gn && gn->name[0] != '\0') {
            strncpy(stops[k].name, gn->name, sizeof(stops[k].name) - 1);
            stops[k].name[sizeof(stops[k].name) - 1] = '\0';
        } else {
            snprintf(stops[k].name, sizeof(stops[k].name), "Stop_%d", node_idx);
        }

        /* Stop classification based on distance to Bangalore landmark centres */
        double d_maj = graph_haversine_m(stops[k].lat, stops[k].lng, HUB_MAJESTIC_LAT, HUB_MAJESTIC_LNG);
        double d_shv = graph_haversine_m(stops[k].lat, stops[k].lng, HUB_SHIVAJINAGAR_LAT, HUB_SHIVAJINAGAR_LNG);

        if (d_maj < 700.0 || d_shv < 700.0) {
            stops[k].type = STOP_HUB;
        } else {
            double d_mg  = graph_haversine_m(stops[k].lat, stops[k].lng, OFFICE_MGROAD_LAT, OFFICE_MGROAD_LNG);
            double d_ind = graph_haversine_m(stops[k].lat, stops[k].lng, OFFICE_INDIRANAGAR_LAT, OFFICE_INDIRANAGAR_LNG);
            double d_kor = graph_haversine_m(stops[k].lat, stops[k].lng, OFFICE_KORAMANGALA_LAT, OFFICE_KORAMANGALA_LNG);

            if (d_mg < 1500.0 || d_ind < 1500.0 || d_kor < 1500.0) {
                stops[k].type = STOP_OFFICE;
            } else {
                double um = demand_unit(demand_hash(seed, (uint64_t)stops[k].osm_id, 0, 556));
                stops[k].type = (um < 0.18) ? STOP_MARKET : STOP_RESIDENTIAL;
            }
        }

        stops[k].scale = demand_stop_scale(seed, stops[k].osm_id);
    }

    free(candidates);
    *out = stops;
    *count = selected_count;
    return STATUS_OK;
}

const AnchorCentre g_anchor_centres[DEMAND_NUM_ANCHOR_CENTRES] = {
    { "Majestic",     12.9767, 77.5713,  700.0, 3, STOP_HUB },
    { "Shivajinagar", 12.9857, 77.6057,  700.0, 3, STOP_HUB },
    { "MG Road",      12.9756, 77.6068, 1500.0, 2, STOP_OFFICE },
    { "Indiranagar",  12.9784, 77.6408, 1500.0, 2, STOP_OFFICE },
    { "Koramangala",  12.9352, 77.6245, 1500.0, 2, STOP_OFFICE }
};

typedef struct CandidateDist {
    int node;
    double dist;
} CandidateDist;

static int cmp_cand_dist(const void *a, const void *b) {
    const CandidateDist *ca = (const CandidateDist *)a;
    const CandidateDist *cb = (const CandidateDist *)b;
    if (ca->dist < cb->dist) return -1;
    if (ca->dist > cb->dist) return 1;
    if (ca->node < cb->node) return -1;
    if (ca->node > cb->node) return 1;
    return 0;
}

static int cmp_int_asc(const void *a, const void *b) {
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    if (ia < ib) return -1;
    if (ia > ib) return 1;
    return 0;
}

Status demand_stops_from_graph_anchored(const Graph *g, size_t max_stops, uint64_t seed, DemandStop **out, size_t *count) {
    if (!g || !out || !count) {
        return STATUS_ERR_NULL;
    }
    *out = NULL;
    *count = 0;

    if (max_stops == 0) {
        return STATUS_ERR_INVALID;
    }

    int *scc_nodes = NULL;
    size_t scc_count = 0;
    Status st = graph_largest_scc(g, &scc_nodes, &scc_count);
    if (st != STATUS_OK || scc_count == 0) {
        free(scc_nodes);
        return (st == STATUS_OK) ? STATUS_ERR_INVALID : st;
    }

    /* Collect candidates: nodes in largest SCC that have is_stop == 1 */
    int *candidates = (int *)malloc(scc_count * sizeof(int));
    if (!candidates) {
        free(scc_nodes);
        return STATUS_ERR_NOMEM;
    }

    size_t cand_count = 0;
    for (size_t i = 0; i < scc_count; i++) {
        int u = scc_nodes[i];
        const GraphNode *gn = graph_node(g, u);
        if (gn && gn->is_stop) {
            candidates[cand_count++] = u;
        }
    }
    free(scc_nodes);

    if (cand_count == 0) {
        free(candidates);
        return STATUS_ERR_INVALID;
    }

    char *is_anchor = (char *)calloc(cand_count, sizeof(char));
    int *anchor_nodes = (int *)malloc(cand_count * sizeof(int));
    CandidateDist *dist_buf = (CandidateDist *)malloc(cand_count * sizeof(CandidateDist));
    if (!is_anchor || !anchor_nodes || !dist_buf) {
        free(candidates);
        free(is_anchor);
        free(anchor_nodes);
        free(dist_buf);
        return STATUS_ERR_NOMEM;
    }

    size_t n_anchors = 0;

    /* For each hub centre (Majestic, then Shivajinagar) take up to 3 nearest candidates within 700 m;
     * for each office centre (MG Road, Indiranagar, Koramangala) up to 2 nearest within 1500 m.
     * Ties broken by smaller node index; a node is taken once. */
    for (size_t c = 0; c < DEMAND_NUM_ANCHOR_CENTRES; c++) {
        const AnchorCentre *ac = &g_anchor_centres[c];
        size_t n_match = 0;

        for (size_t i = 0; i < cand_count; i++) {
            if (is_anchor[i]) {
                continue; /* a node is taken once */
            }
            int u = candidates[i];
            const GraphNode *gn = graph_node(g, u);
            if (!gn) continue;
            double d = graph_haversine_m(gn->lat, gn->lng, ac->lat, ac->lng);
            if (d <= ac->radius_m) {
                dist_buf[n_match].node = u;
                dist_buf[n_match].dist = d;
                n_match++;
            }
        }

        if (n_match > 0) {
            qsort(dist_buf, n_match, sizeof(CandidateDist), cmp_cand_dist);
            size_t take = (n_match < ac->max_anchors) ? n_match : ac->max_anchors;
            for (size_t j = 0; j < take; j++) {
                int u = dist_buf[j].node;
                anchor_nodes[n_anchors++] = u;
                for (size_t i = 0; i < cand_count; i++) {
                    if (candidates[i] == u) {
                        is_anchor[i] = 1;
                        break;
                    }
                }
            }
        }
    }
    free(dist_buf);

    int *selected_nodes = (int *)malloc((cand_count < max_stops ? cand_count : max_stops) * sizeof(int));
    if (!selected_nodes) {
        free(candidates);
        free(is_anchor);
        free(anchor_nodes);
        return STATUS_ERR_NOMEM;
    }

    size_t n_selected = 0;

    /* If max_stops is smaller than the number of anchors, keep anchors in this order (hubs first, then offices) truncated to max_stops */
    if (max_stops < n_anchors) {
        for (size_t i = 0; i < max_stops; i++) {
            selected_nodes[n_selected++] = anchor_nodes[i];
        }
    } else {
        for (size_t i = 0; i < n_anchors; i++) {
            selected_nodes[n_selected++] = anchor_nodes[i];
        }

        size_t rem_slots = max_stops - n_anchors;
        int *rem_candidates = (int *)malloc(cand_count * sizeof(int));
        if (!rem_candidates) {
            free(candidates);
            free(is_anchor);
            free(anchor_nodes);
            free(selected_nodes);
            return STATUS_ERR_NOMEM;
        }

        size_t rem_count = 0;
        for (size_t i = 0; i < cand_count; i++) {
            if (!is_anchor[i]) {
                rem_candidates[rem_count++] = candidates[i];
            }
        }

        /* Remaining slots rem_slots = max_stops - anchors are filled by stride over the remaining candidates (anchors excluded):
         * index_k = (k * rem_count) / rem_slots (take all if rem_count <= rem_slots). */
        if (rem_count <= rem_slots) {
            for (size_t i = 0; i < rem_count; i++) {
                selected_nodes[n_selected++] = rem_candidates[i];
            }
        } else {
            for (size_t k = 0; k < rem_slots; k++) {
                size_t index_k = (k * rem_count) / rem_slots;
                selected_nodes[n_selected++] = rem_candidates[index_k];
            }
        }
        free(rem_candidates);
    }

    free(candidates);
    free(is_anchor);
    free(anchor_nodes);

    /* Final list sorted ascending by node index */
    qsort(selected_nodes, n_selected, sizeof(int), cmp_int_asc);

    DemandStop *stops = (DemandStop *)calloc(n_selected, sizeof(DemandStop));
    if (!stops) {
        free(selected_nodes);
        return STATUS_ERR_NOMEM;
    }

    for (size_t k = 0; k < n_selected; k++) {
        int node_idx = selected_nodes[k];
        const GraphNode *gn = graph_node(g, node_idx);

        stops[k].node = node_idx;
        stops[k].osm_id = gn ? gn->osm_id : 0;
        stops[k].lat = gn ? gn->lat : 0.0;
        stops[k].lng = gn ? gn->lng : 0.0;
        if (gn && gn->name[0] != '\0') {
            strncpy(stops[k].name, gn->name, sizeof(stops[k].name) - 1);
            stops[k].name[sizeof(stops[k].name) - 1] = '\0';
        } else {
            snprintf(stops[k].name, sizeof(stops[k].name), "Stop_%d", node_idx);
        }

        /* Stop classification based on distance to Bangalore landmark centres.
         * Note: an anchor within 700 m of a hub centre becomes HUB by rule order. */
        double d_maj = graph_haversine_m(stops[k].lat, stops[k].lng, HUB_MAJESTIC_LAT, HUB_MAJESTIC_LNG);
        double d_shv = graph_haversine_m(stops[k].lat, stops[k].lng, HUB_SHIVAJINAGAR_LAT, HUB_SHIVAJINAGAR_LNG);

        if (d_maj < 700.0 || d_shv < 700.0) {
            stops[k].type = STOP_HUB;
        } else {
            double d_mg  = graph_haversine_m(stops[k].lat, stops[k].lng, OFFICE_MGROAD_LAT, OFFICE_MGROAD_LNG);
            double d_ind = graph_haversine_m(stops[k].lat, stops[k].lng, OFFICE_INDIRANAGAR_LAT, OFFICE_INDIRANAGAR_LNG);
            double d_kor = graph_haversine_m(stops[k].lat, stops[k].lng, OFFICE_KORAMANGALA_LAT, OFFICE_KORAMANGALA_LNG);

            if (d_mg < 1500.0 || d_ind < 1500.0 || d_kor < 1500.0) {
                stops[k].type = STOP_OFFICE;
            } else {
                double um = demand_unit(demand_hash(seed, (uint64_t)stops[k].osm_id, 0, 556));
                stops[k].type = (um < 0.18) ? STOP_MARKET : STOP_RESIDENTIAL;
            }
        }

        stops[k].scale = demand_stop_scale(seed, stops[k].osm_id);
    }

    free(selected_nodes);
    *out = stops;
    *count = n_selected;
    return STATUS_OK;
}

static inline double next_unit(uint64_t *state) {
    *state = mix64(*state);
    return demand_unit(*state);
}

double demand_poisson_sample(double lambda, uint64_t stream_hash) {
    if (lambda <= 0.0) {
        return 0.0;
    }
    uint64_t state = stream_hash;
    if (lambda < 30.0) {
        double L = exp(-lambda);
        int k = 0;
        double p = 1.0;
        do {
            k++;
            p *= next_unit(&state);
        } while (p > L);
        return (double)(k - 1);
    } else {
        double u1 = next_unit(&state);
        double u2 = next_unit(&state);
        if (u1 < 1e-300) u1 = 1e-300;
        double z = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
        double val = floor(lambda + sqrt(lambda) * z + 0.5);
        if (val < 0.0) val = 0.0;
        return val;
    }
}

Status demand_generate(const DemandStop *stops, size_t n_stops, uint64_t seed, int days, DemandRow **rows_out, size_t *n_rows) {
    if (!stops || !rows_out || !n_rows) {
        return STATUS_ERR_NULL;
    }
    *rows_out = NULL;
    *n_rows = 0;

    if (n_stops == 0 || days < 1 || days > 3650) {
        return STATUS_ERR_INVALID;
    }

    size_t total_rows = (size_t)days * n_stops * 24;
    DemandRow *rows = (DemandRow *)malloc(total_rows * sizeof(DemandRow));
    if (!rows) {
        return STATUS_ERR_NOMEM;
    }

    size_t row_idx = 0;
    for (int day = 0; day < days; day++) {
        DayType day_type = DAY_WEEKDAY;
        int rain = 0;
        int event_day = 0;
        demand_calendar(seed, day, &day_type, &rain, &event_day);

        int ev_stops[3];
        size_t ev_cnt = 0;
        if (event_day) {
            ev_cnt = demand_event_stops(seed, day, n_stops, ev_stops);
        }

        for (size_t si = 0; si < n_stops; si++) {
            int is_ev_stop = 0;
            for (size_t ei = 0; ei < ev_cnt; ei++) {
                if (ev_stops[ei] == (int)si) {
                    is_ev_stop = 1;
                    break;
                }
            }

            for (int hour = 0; hour < 24; hour++) {
                int event = (is_ev_stop && hour >= 17 && hour <= 20) ? 1 : 0;
                double lambda = demand_expected(stops[si].type, stops[si].scale, hour, day_type, rain, event);
                uint64_t stream_h = demand_hash(seed, (uint64_t)day, (uint64_t)(si * 24 + (size_t)hour), 777);
                double passengers = demand_poisson_sample(lambda, stream_h);

                rows[row_idx].day = day;
                rows[row_idx].stop_node = stops[si].node;
                rows[row_idx].stop_type = stops[si].type;
                rows[row_idx].stop_scale = stops[si].scale;
                rows[row_idx].hour = hour;
                rows[row_idx].day_type = day_type;
                rows[row_idx].rain = rain;
                rows[row_idx].event = event;
                rows[row_idx].passengers = passengers;
                rows[row_idx].lambda = lambda;

                row_idx++;
            }
        }
    }

    *rows_out = rows;
    *n_rows = total_rows;
    return STATUS_OK;
}

static Status atomic_file_replace(const char *tmp_path, const char *dst_path) {
    remove(dst_path);
    if (rename(tmp_path, dst_path) != 0) {
        remove(tmp_path);
        return STATUS_ERR_INVALID;
    }
    return STATUS_OK;
}

Status demand_rows_write_csv(const DemandRow *rows, size_t n, const char *path) {
    if (!rows || !path) {
        return STATUS_ERR_NULL;
    }

    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        return STATUS_ERR_INVALID;
    }

    if (fprintf(f, "day,stop_node,stop_type,stop_scale,hour,day_type,rain,event,passengers,lambda\n") < 0) {
        fclose(f);
        remove(tmp_path);
        return STATUS_ERR_INVALID;
    }

    for (size_t i = 0; i < n; i++) {
        const DemandRow *r = &rows[i];
        if (fprintf(f, "%d,%d,%d,%.6f,%d,%d,%d,%d,%.0f,%.9g\n",
                    r->day, r->stop_node, (int)r->stop_type, r->stop_scale,
                    r->hour, (int)r->day_type, r->rain, r->event,
                    r->passengers, r->lambda) < 0) {
            fclose(f);
            remove(tmp_path);
            return STATUS_ERR_INVALID;
        }
    }

    if (fclose(f) != 0) {
        remove(tmp_path);
        return STATUS_ERR_INVALID;
    }

    return atomic_file_replace(tmp_path, path);
}

static char *next_csv_field(char **cursor) {
    if (!cursor || !*cursor) return NULL;
    char *start = *cursor;
    char *p = start;
    while (*p && *p != ',') {
        p++;
    }
    if (*p == ',') {
        *p = '\0';
        *cursor = p + 1;
    } else {
        *cursor = NULL;
    }
    return start;
}

Status demand_rows_load_csv(const char *path, DemandRow **rows_out, size_t *n_out) {
    if (!path || !rows_out || !n_out) {
        return STATUS_ERR_NULL;
    }
    *rows_out = NULL;
    *n_out = 0;

    FILE *f = fopen(path, "rb");
    if (!f) {
        return STATUS_ERR_NOT_FOUND;
    }

    char line[514];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return STATUS_ERR_INVALID; /* empty file */
    }

    /* Check length */
    size_t len = strlen(line);
    if (len > 512 || (len > 0 && line[len - 1] != '\n' && !feof(f))) {
        fclose(f);
        return STATUS_ERR_INVALID;
    }

    /* Strip CRLF */
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
        line[--len] = '\0';
    }

    static const char EXPECTED_HEADER[] = "day,stop_node,stop_type,stop_scale,hour,day_type,rain,event,passengers,lambda";
    if (strcmp(line, EXPECTED_HEADER) != 0) {
        fclose(f);
        return STATUS_ERR_INVALID;
    }

    size_t cap = 256;
    size_t count = 0;
    DemandRow *rows = (DemandRow *)malloc(cap * sizeof(DemandRow));
    if (!rows) {
        fclose(f);
        return STATUS_ERR_NOMEM;
    }

    int parse_error = 0;

    while (fgets(line, sizeof(line), f)) {
        size_t l_len = strlen(line);
        if (l_len > 512 || (l_len > 0 && line[l_len - 1] != '\n' && !feof(f))) {
            parse_error = 1;
            break;
        }
        while (l_len > 0 && (line[l_len - 1] == '\r' || line[l_len - 1] == '\n')) {
            line[--l_len] = '\0';
        }
        if (l_len == 0) continue;

        char *cursor = line;
        char *tok = next_csv_field(&cursor);
        if (!tok) { parse_error = 1; break; }

        char *endptr = NULL;
        long day_val = strtol(tok, &endptr, 10);
        if (*endptr != '\0' || day_val < 0) { parse_error = 1; break; }

        tok = next_csv_field(&cursor);
        if (!tok) { parse_error = 1; break; }
        long stop_node = strtol(tok, &endptr, 10);
        if (*endptr != '\0' || stop_node < 0) { parse_error = 1; break; }

        tok = next_csv_field(&cursor);
        if (!tok) { parse_error = 1; break; }
        long st_val = strtol(tok, &endptr, 10);
        if (*endptr != '\0' || st_val < 0 || st_val > 3) { parse_error = 1; break; }

        tok = next_csv_field(&cursor);
        if (!tok) { parse_error = 1; break; }
        double scale = strtod(tok, &endptr);
        if (*endptr != '\0' || scale <= 0.0) { parse_error = 1; break; }

        tok = next_csv_field(&cursor);
        if (!tok) { parse_error = 1; break; }
        long hour = strtol(tok, &endptr, 10);
        if (*endptr != '\0' || hour < 0 || hour > 23) { parse_error = 1; break; }

        tok = next_csv_field(&cursor);
        if (!tok) { parse_error = 1; break; }
        long dt_val = strtol(tok, &endptr, 10);
        if (*endptr != '\0' || dt_val < 0 || dt_val > 2) { parse_error = 1; break; }

        tok = next_csv_field(&cursor);
        if (!tok) { parse_error = 1; break; }
        long rain = strtol(tok, &endptr, 10);
        if (*endptr != '\0' || (rain != 0 && rain != 1)) { parse_error = 1; break; }

        tok = next_csv_field(&cursor);
        if (!tok) { parse_error = 1; break; }
        long ev = strtol(tok, &endptr, 10);
        if (*endptr != '\0' || (ev != 0 && ev != 1)) { parse_error = 1; break; }

        tok = next_csv_field(&cursor);
        if (!tok) { parse_error = 1; break; }
        double pass = strtod(tok, &endptr);
        if (*endptr != '\0' || pass < 0.0) { parse_error = 1; break; }

        tok = next_csv_field(&cursor);
        if (!tok) { parse_error = 1; break; }
        double lambda = strtod(tok, &endptr);
        if (*endptr != '\0' || lambda < 0.0) { parse_error = 1; break; }

        /* Ensure no extra tokens */
        if (cursor != NULL) { parse_error = 1; break; }

        if (count >= cap) {
            cap *= 2;
            DemandRow *new_rows = (DemandRow *)realloc(rows, cap * sizeof(DemandRow));
            if (!new_rows) {
                free(rows);
                fclose(f);
                return STATUS_ERR_NOMEM;
            }
            rows = new_rows;
        }

        rows[count].day = (int)day_val;
        rows[count].stop_node = (int)stop_node;
        rows[count].stop_type = (StopType)st_val;
        rows[count].stop_scale = scale;
        rows[count].hour = (int)hour;
        rows[count].day_type = (DayType)dt_val;
        rows[count].rain = (int)rain;
        rows[count].event = (int)ev;
        rows[count].passengers = pass;
        rows[count].lambda = lambda;
        count++;
    }

    fclose(f);

    if (parse_error) {
        free(rows);
        return STATUS_ERR_INVALID;
    }

    *rows_out = rows;
    *n_out = count;
    return STATUS_OK;
}

DemandCategory demand_row_category(const DemandRow *r) {
    if (!r) return CAT_WEEKDAY;
    if (r->event != 0) {
        return CAT_EVENT;
    }
    if (r->rain != 0) {
        return CAT_RAIN;
    }
    if (r->day_type == DAY_HOLIDAY) {
        return CAT_HOLIDAY;
    }
    if (r->day_type == DAY_WEEKEND) {
        return CAT_WEEKEND;
    }
    return CAT_WEEKDAY;
}

const char *demand_category_name(DemandCategory c) {
    switch (c) {
        case CAT_WEEKDAY: return "weekday";
        case CAT_WEEKEND: return "weekend";
        case CAT_HOLIDAY: return "holiday";
        case CAT_RAIN:    return "rain";
        case CAT_EVENT:   return "event";
        default:          return "unknown";
    }
}
