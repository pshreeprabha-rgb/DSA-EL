#define _POSIX_C_SOURCE 200809L

#include "osm_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#ifdef __has_include
#  if __has_include(<cjson/cJSON.h>)
#    include <cjson/cJSON.h>
#  else
#    include <cJSON.h>
#  endif
#else
#  include <cjson/cJSON.h>
#endif

#include <curl/curl.h>
#include "hashtable.h"

/* ---- Overpass query constants ---- */
static const char *OVERPASS_ROADS_TMPL =
    "[out:json][timeout:90];"
    "way[\"highway\"~\"^(primary|secondary|tertiary)(_link)?$\"](%s);(._;>;);out body;";

static const char *OVERPASS_STOPS_TMPL =
    "[out:json][timeout:60];"
    "node[\"highway\"=\"bus_stop\"](%s);out body;";

static const double SNAP_RADIUS_M = 150.0;

void osm_config_default(OsmConfig *c) {
    if (!c) return;
    c->south = 12.92;
    c->west  = 77.54;
    c->north = 13.02;
    c->east  = 77.66;
    c->overpass_url = "https://overpass-api.de/api/interpreter";
    c->data_dir     = "data";
    c->user_agent   = "SmartTransportStudentProject/1.0";
    c->timeout_s    = 90;
}

/* ---- Internal node info used during build ---- */
typedef struct OsmNode {
    double lat, lng;
    int64_t osm_id;
} OsmNode;

/* Build the bbox string "S,W,N,E" */
static void bbox_str(char *buf, size_t n, const OsmConfig *c) {
    snprintf(buf, n, "%.5f,%.5f,%.5f,%.5f", c->south, c->west, c->north, c->east);
}

/* Map highway tag to RoadClass */
static RoadClass highway_to_class(const char *h) {
    if (!h) return ROAD_OTHER;
    if (strstr(h, "primary"))   return ROAD_PRIMARY;
    if (strstr(h, "secondary")) return ROAD_SECONDARY;
    if (strstr(h, "tertiary"))  return ROAD_TERTIARY;
    return ROAD_OTHER;
}

/* Free-flow speed in km/h from maxspeed tag and highway class */
static double get_freeflow_kmh(const char *maxspeed_tag, const char *highway_tag) {
    if (maxspeed_tag && maxspeed_tag[0]) {
        char *endptr = NULL;
        long v = strtol(maxspeed_tag, &endptr, 10);
        if (endptr != maxspeed_tag && *endptr == '\0' && v > 0) {
            if (v < 10) v = 10;
            if (v > 80) v = 80;
            return (double)v;
        }
    }
    if (!highway_tag) return 25.0;
    if (strstr(highway_tag, "primary_link"))   return 20.0;
    if (strstr(highway_tag, "secondary_link"))  return 20.0;
    if (strstr(highway_tag, "tertiary_link"))   return 20.0;
    if (strstr(highway_tag, "primary"))   return 35.0;
    if (strstr(highway_tag, "secondary")) return 30.0;
    if (strstr(highway_tag, "tertiary"))  return 25.0;
    return 25.0;
}

Status osm_build_graph_from_json(const char *roads_json, const char *stops_json,
                                  Graph **out, OsmBuildStats *stats) {
    if (!roads_json || !out) return STATUS_ERR_NULL;
    *out = NULL;

    OsmBuildStats st = {0};

    /* ---- Parse roads_json ---- */
    cJSON *roads_root = cJSON_Parse(roads_json);
    if (!roads_root) return STATUS_ERR_INVALID;

    cJSON *elements = cJSON_GetObjectItemCaseSensitive(roads_root, "elements");
    if (!elements || !cJSON_IsArray(elements)) {
        cJSON_Delete(roads_root);
        return STATUS_ERR_INVALID;
    }

    /* Step 1a: Collect node coordinates into HT: osm_id -> OsmNode* */
    HashTable *node_coords = ht_create(HT_KEY_INT, free);
    if (!node_coords) { cJSON_Delete(roads_root); return STATUS_ERR_NOMEM; }

    /* Step 1b: Collect ways */
    typedef struct Way {
        int64_t *node_ids;
        size_t node_count;
        char highway[32];
        char name[48];
        char ref[24];
        char maxspeed[16];
        int oneway;      /* 1=forward, -1=reverse, 0=bidirectional */
        int roundabout;  /* 1 if junction=roundabout */
    } Way;

    size_t ways_cap = 64;
    size_t ways_count = 0;
    Way *ways = (Way *)malloc(ways_cap * sizeof(Way));
    if (!ways) { ht_destroy(node_coords); cJSON_Delete(roads_root); return STATUS_ERR_NOMEM; }

    int parse_ok = 1;

    cJSON *elem = NULL;
    /* Pass 1: Parse and record all nodes in node_coords */
    cJSON_ArrayForEach(elem, elements) {
        if (!cJSON_IsObject(elem)) continue;
        cJSON *type_j = cJSON_GetObjectItemCaseSensitive(elem, "type");
        if (!type_j || !cJSON_IsString(type_j)) continue;
        if (strcmp(type_j->valuestring, "node") != 0) continue;

        cJSON *lat_j = cJSON_GetObjectItemCaseSensitive(elem, "lat");
        cJSON *lon_j = cJSON_GetObjectItemCaseSensitive(elem, "lon");
        cJSON *id_j  = cJSON_GetObjectItemCaseSensitive(elem, "id");
        if (!lat_j || !lon_j || !id_j) continue;
        if (!cJSON_IsNumber(lat_j) || !cJSON_IsNumber(lon_j) || !cJSON_IsNumber(id_j)) continue;

        OsmNode *n = (OsmNode *)malloc(sizeof(OsmNode));
        if (!n) { parse_ok = 0; break; }
        n->osm_id = (int64_t)cJSON_GetNumberValue(id_j);
        n->lat = cJSON_GetNumberValue(lat_j);
        n->lng = cJSON_GetNumberValue(lon_j);
        ht_put_int(node_coords, n->osm_id, n);
        st.json_nodes++;
    }

    if (!parse_ok) {
        free(ways);
        ht_destroy(node_coords);
        cJSON_Delete(roads_root);
        return STATUS_ERR_NOMEM;
    }

    /* Pass 2: Parse ways (ways_total = all way elements parsed; ways with missing node IDs skipped) */
    cJSON_ArrayForEach(elem, elements) {
        if (!cJSON_IsObject(elem)) continue;
        cJSON *type_j = cJSON_GetObjectItemCaseSensitive(elem, "type");
        if (!type_j || !cJSON_IsString(type_j)) continue;
        if (strcmp(type_j->valuestring, "way") != 0) continue;

        cJSON *nodes_j = cJSON_GetObjectItemCaseSensitive(elem, "nodes");
        if (!nodes_j || !cJSON_IsArray(nodes_j)) continue;
        st.ways_total++;

        cJSON *tags_j = cJSON_GetObjectItemCaseSensitive(elem, "tags");
        if (!tags_j) continue;
        cJSON *hw_j = cJSON_GetObjectItemCaseSensitive(tags_j, "highway");
        if (!hw_j || !cJSON_IsString(hw_j)) continue;

        const char *hw = hw_j->valuestring;
        if (!strstr(hw, "primary") && !strstr(hw, "secondary") && !strstr(hw, "tertiary")) continue;

        size_t nc = (size_t)cJSON_GetArraySize(nodes_j);
        if (nc < 2) continue;

        /* A way that references a node id missing from the JSON is skipped ENTIRELY */
        int missing_node = 0;
        for (size_t ni = 0; ni < nc; ni++) {
            cJSON *nid = cJSON_GetArrayItem(nodes_j, (int)ni);
            if (!nid || !cJSON_IsNumber(nid)) { missing_node = 1; break; }
            int64_t node_id = (int64_t)cJSON_GetNumberValue(nid);
            if (!ht_get_int(node_coords, node_id)) {
                missing_node = 1;
                break;
            }
        }
        if (missing_node) continue;

        if (ways_count >= ways_cap) {
            ways_cap *= 2;
            Way *nw = (Way *)realloc(ways, ways_cap * sizeof(Way));
            if (!nw) { parse_ok = 0; break; }
            ways = nw;
        }

        Way *w = &ways[ways_count];
        memset(w, 0, sizeof(Way));
        strncpy(w->highway, hw, sizeof(w->highway) - 1);

        cJSON *name_j = cJSON_GetObjectItemCaseSensitive(tags_j, "name");
        if (name_j && cJSON_IsString(name_j)) strncpy(w->name, name_j->valuestring, sizeof(w->name) - 1);
        else {
            cJSON *ref_j = cJSON_GetObjectItemCaseSensitive(tags_j, "ref");
            if (ref_j && cJSON_IsString(ref_j)) strncpy(w->name, ref_j->valuestring, sizeof(w->name) - 1);
        }

        cJSON *ms_j = cJSON_GetObjectItemCaseSensitive(tags_j, "maxspeed");
        if (ms_j && cJSON_IsString(ms_j)) strncpy(w->maxspeed, ms_j->valuestring, sizeof(w->maxspeed) - 1);

        /* Oneway */
        cJSON *ow_j = cJSON_GetObjectItemCaseSensitive(tags_j, "oneway");
        cJSON *jn_j = cJSON_GetObjectItemCaseSensitive(tags_j, "junction");
        if (jn_j && cJSON_IsString(jn_j) && strcmp(jn_j->valuestring, "roundabout") == 0) {
            w->roundabout = 1;
            w->oneway = 1;
        }
        if (ow_j && cJSON_IsString(ow_j)) {
            const char *ov = ow_j->valuestring;
            if (strcmp(ov, "yes") == 0 || strcmp(ov, "true") == 0 || strcmp(ov, "1") == 0) {
                w->oneway = 1;
            } else if (strcmp(ov, "-1") == 0) {
                w->oneway = -1;
            }
        }

        w->node_ids = (int64_t *)malloc(nc * sizeof(int64_t));
        if (!w->node_ids) { parse_ok = 0; break; }
        w->node_count = nc;
        for (size_t ni = 0; ni < nc; ni++) {
            cJSON *nid = cJSON_GetArrayItem(nodes_j, (int)ni);
            w->node_ids[ni] = (int64_t)cJSON_GetNumberValue(nid);
        }

        ways_count++;
    }

    if (!parse_ok) {
        for (size_t i = 0; i < ways_count; i++) free(ways[i].node_ids);
        free(ways);
        ht_destroy(node_coords);
        cJSON_Delete(roads_root);
        return STATUS_ERR_NOMEM;
    }

    /* Step 2: Count node occurrences to find junctions */
    HashTable *node_occ = ht_create(HT_KEY_INT, free);  /* osm_id -> int* count */
    if (!node_occ) {
        for (size_t i = 0; i < ways_count; i++) free(ways[i].node_ids);
        free(ways);
        ht_destroy(node_coords);
        cJSON_Delete(roads_root);
        return STATUS_ERR_NOMEM;
    }

    for (size_t wi = 0; wi < ways_count; wi++) {
        Way *w = &ways[wi];
        for (size_t ni = 0; ni < w->node_count; ni++) {
            int64_t nid = w->node_ids[ni];
            /* First or last node of way -> junction */
            int is_endpoint = (ni == 0 || ni == w->node_count - 1);

            int *cnt = (int *)ht_get_int(node_occ, nid);
            if (!cnt) {
                cnt = (int *)calloc(1, sizeof(int));
                if (cnt) ht_put_int(node_occ, nid, cnt);
            }
            if (cnt) {
                (*cnt)++;
                /* Mark endpoints with extra large value to force junction */
                if (is_endpoint && *cnt < 100) *cnt += 100;
            }
        }
    }

    /* Build the raw graph */
    Graph *g = graph_create();
    if (!g) {
        for (size_t i = 0; i < ways_count; i++) free(ways[i].node_ids);
        free(ways);
        ht_destroy(node_coords);
        ht_destroy(node_occ);
        cJSON_Delete(roads_root);
        return STATUS_ERR_NOMEM;
    }

    /* Step 3: Walk each way and add junction->junction edges */
    for (size_t wi = 0; wi < ways_count; wi++) {
        Way *w = &ways[wi];
        if (w->node_count < 2) continue;

        RoadClass cls = highway_to_class(w->highway);
        double kmh = get_freeflow_kmh(w->maxspeed, w->highway);
        double mps = kmh / 3.6;

        /* Scan for segment start (must be junction) */
        size_t seg_start = 0;
        int way_produced_edge = 0;
        /* First node must be in graph (is endpoint, hence junction) */

        for (size_t ni = 0; ni < w->node_count - 1; ) {
            /* Find the next junction after ni */
            int64_t start_id = w->node_ids[seg_start];
            OsmNode *start_node = (OsmNode *)ht_get_int(node_coords, start_id);
            if (!start_node) { seg_start++; ni = seg_start; continue; }

            double seg_len = 0.0;
            size_t end_idx = seg_start + 1;
            while (end_idx < w->node_count) {
                int64_t prev_id = w->node_ids[end_idx - 1];
                int64_t cur_id  = w->node_ids[end_idx];
                OsmNode *pn = (OsmNode *)ht_get_int(node_coords, prev_id);
                OsmNode *cn = (OsmNode *)ht_get_int(node_coords, cur_id);
                if (!pn || !cn) break;
                seg_len += graph_haversine_m(pn->lat, pn->lng, cn->lat, cn->lng);

                /* Is cur_id a junction? */
                int *cnt = (int *)ht_get_int(node_occ, cur_id);
                int is_junction = !cnt || (*cnt > 1) || (end_idx == w->node_count - 1);
                if (is_junction || end_idx == w->node_count - 1) break;
                end_idx++;
            }

            if (end_idx >= w->node_count) end_idx = w->node_count - 1;

            int64_t end_id = w->node_ids[end_idx];
            OsmNode *end_node = (OsmNode *)ht_get_int(node_coords, end_id);

            if (start_node && end_node && seg_len > 0.0 && start_id != end_id) {
                int u = graph_add_node(g, start_id, start_node->lat, start_node->lng, "", 0);
                int v = graph_add_node(g, end_id,   end_node->lat,   end_node->lng,   "", 0);

                if (u >= 0 && v >= 0 && u != v) {
                    double base_time_s = seg_len / mps;
                    if (w->oneway == 1) {
                        graph_add_edge(g, u, v, seg_len, base_time_s, w->name, cls);
                    } else if (w->oneway == -1) {
                        graph_add_edge(g, v, u, seg_len, base_time_s, w->name, cls);
                    } else {
                        graph_add_bidirectional(g, u, v, seg_len, base_time_s, w->name, cls, NULL, NULL);
                    }
                    st.edges_created++;
                    way_produced_edge = 1;
                }
            }

            seg_start = end_idx;
            ni = seg_start;
        }

        if (way_produced_edge) {
            st.ways_used++;
        }
    }

    /* Clean up ways */
    for (size_t i = 0; i < ways_count; i++) free(ways[i].node_ids);
    free(ways);
    ht_destroy(node_occ);

    st.junction_nodes = graph_node_count(g);

    /* Step 6: Extract largest component */
    size_t before = graph_node_count(g);
    Graph *lcc = graph_extract_largest_component(g);
    graph_destroy(g);

    if (!lcc) {
        ht_destroy(node_coords);
        cJSON_Delete(roads_root);
        return STATUS_ERR_INVALID;
    }
    st.nodes_dropped_by_component = before - graph_node_count(lcc);
    st.final_nodes = graph_node_count(lcc);
    st.final_edges = graph_edge_count(lcc);

    /* Step 7: Snap bus stops (if stops_json provided) */
    if (stops_json && stops_json[0]) {
        cJSON *stops_root = cJSON_Parse(stops_json);
        if (stops_root) {
            cJSON *se = cJSON_GetObjectItemCaseSensitive(stops_root, "elements");
            if (se && cJSON_IsArray(se)) {
                cJSON *stop_elem = NULL;
                cJSON_ArrayForEach(stop_elem, se) {
                    cJSON *type_j = cJSON_GetObjectItemCaseSensitive(stop_elem, "type");
                    if (!type_j || !cJSON_IsString(type_j) || strcmp(type_j->valuestring, "node") != 0) continue;
                    cJSON *lat_j = cJSON_GetObjectItemCaseSensitive(stop_elem, "lat");
                    cJSON *lon_j = cJSON_GetObjectItemCaseSensitive(stop_elem, "lon");
                    if (!lat_j || !lon_j || !cJSON_IsNumber(lat_j) || !cJSON_IsNumber(lon_j)) continue;

                    double slat = cJSON_GetNumberValue(lat_j);
                    double slng = cJSON_GetNumberValue(lon_j);
                    st.stops_found++;

                    double dist_m = 0.0;
                    int nearest = graph_nearest_node(lcc, slat, slng, &dist_m);
                    if (nearest < 0 || dist_m > SNAP_RADIUS_M) {
                        st.stops_discarded++;
                        continue;
                    }

                    /* Mark node as a stop; copy name if empty */
                    GraphNode *gn = NULL;
                    {
                        const GraphNode *cgn = graph_node(lcc, nearest);
                        (void)cgn; /* update via graph_add_node dedup */
                    }
                    int64_t node_osm_id = graph_node(lcc, nearest)->osm_id;

                    const char *stop_name = NULL;
                    cJSON *tags_j = cJSON_GetObjectItemCaseSensitive(stop_elem, "tags");
                    if (tags_j) {
                        cJSON *name_j = cJSON_GetObjectItemCaseSensitive(tags_j, "name");
                        if (name_j && cJSON_IsString(name_j)) stop_name = name_j->valuestring;
                    }

                    /* Use the dedup path to set is_stop and optionally fill name */
                    graph_add_node(lcc, node_osm_id, slat, slng, stop_name ? stop_name : "", 1);
                    (void)gn;
                    st.stops_snapped++;
                }
            }
            cJSON_Delete(stops_root);
        }
    }

    ht_destroy(node_coords);
    cJSON_Delete(roads_root);

    if (stats) *stats = st;
    *out = lcc;
    return STATUS_OK;
}

/* ---- libcurl fetch for Overpass ---- */

typedef struct CurlBuf2 { char *data; size_t size; size_t cap; } CurlBuf2;

static size_t osm_curl_write(char *ptr, size_t size, size_t nmemb, void *ud) {
    CurlBuf2 *buf = (CurlBuf2 *)ud;
    size_t in = size * nmemb;
    if (buf->size + in + 1 > buf->cap) {
        size_t nc = buf->cap == 0 ? 65536 : buf->cap * 2;
        while (nc < buf->size + in + 1) nc *= 2;
        if (nc > 128ULL * 1024 * 1024) return 0; /* 128 MB cap */
        char *nd = (char *)realloc(buf->data, nc);
        if (!nd) return 0;
        buf->data = nd;
        buf->cap = nc;
    }
    memcpy(buf->data + buf->size, ptr, in);
    buf->size += in;
    buf->data[buf->size] = '\0';
    return in;
}

Status osm_fetch_raw(const OsmConfig *c, const char *query, char **body_out) {
    if (!c || !query || !body_out) return STATUS_ERR_NULL;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    if (!curl) { curl_global_cleanup(); return STATUS_ERR_INVALID; }

    CurlBuf2 buf = {NULL, 0, 0};
    char *escaped = curl_easy_escape(curl, query, 0);
    if (!escaped) {
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return STATUS_ERR_INVALID;
    }

    size_t post_len = strlen("data=") + strlen(escaped) + 1;
    char *postdata = (char *)malloc(post_len);
    if (!postdata) {
        curl_free(escaped);
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return STATUS_ERR_NOMEM;
    }
    snprintf(postdata, post_len, "data=%s", escaped);
    curl_free(escaped);

    curl_easy_setopt(curl, CURLOPT_URL, c->overpass_url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, osm_curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)c->timeout_s);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, c->user_agent);

    int attempts = 0;
    Status result = STATUS_ERR_INVALID;
    static const int sleep_secs[] = {5, 15};

    while (attempts <= 2) {
        buf.size = 0;
        CURLcode res = curl_easy_perform(curl);
        long http_status = 0;
        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
        }

        if (res == CURLE_OK && http_status == 200) {
            result = STATUS_OK;
            break;
        }

        if (attempts < 2) {
            int should_retry = (res != CURLE_OK) ||
                               (http_status == 429 || http_status == 504);
            if (!should_retry) break;
            sleep((unsigned int)sleep_secs[attempts]);
        }
        attempts++;
    }

    free(postdata);
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    if (result == STATUS_OK) {
        *body_out = buf.data;
    } else {
        free(buf.data);
    }
    return result;
}

Status osm_load_or_fetch(const OsmConfig *c, int refresh, int offline, Graph **out, OsmBuildStats *stats) {
    if (!c || !out) return STATUS_ERR_NULL;

    char cache_path[512];
    char roads_path[512];
    char stops_path[512];
    snprintf(cache_path, sizeof(cache_path), "%s/graph_cache.txt", c->data_dir);
    snprintf(roads_path, sizeof(roads_path), "%s/osm_roads.json", c->data_dir);
    snprintf(stops_path, sizeof(stops_path), "%s/osm_stops.json", c->data_dir);

    /* (a) Try cache */
    if (!refresh) {
        Status st = graph_load(out, cache_path);
        if (st == STATUS_OK) {
            if (stats) memset(stats, 0, sizeof(OsmBuildStats));
            if (*out) {
                if (stats) {
                    stats->final_nodes = graph_node_count(*out);
                    stats->final_edges = graph_edge_count(*out);
                }
            }
            return STATUS_OK;
        }
    }

    /* (b) Try raw JSON files */
    if (!refresh) {
        FILE *fr = fopen(roads_path, "r");
        FILE *fs = fopen(stops_path, "r");
        if (fr) {
            fseek(fr, 0, SEEK_END);
            long roads_size = ftell(fr);
            fseek(fr, 0, SEEK_SET);
            char *roads_buf = (char *)malloc((size_t)roads_size + 1);
            if (roads_buf) {
                fread(roads_buf, 1, (size_t)roads_size, fr);
                roads_buf[roads_size] = '\0';

                char *stops_buf = NULL;
                if (fs) {
                    fseek(fs, 0, SEEK_END);
                    long stops_size = ftell(fs);
                    fseek(fs, 0, SEEK_SET);
                    stops_buf = (char *)malloc((size_t)stops_size + 1);
                    if (stops_buf) {
                        fread(stops_buf, 1, (size_t)stops_size, fs);
                        stops_buf[stops_size] = '\0';
                    }
                }

                Status st = osm_build_graph_from_json(roads_buf, stops_buf, out, stats);
                free(roads_buf);
                free(stops_buf);
                fclose(fr);
                if (fs) fclose(fs);

                if (st == STATUS_OK) {
                    graph_save(*out, cache_path);
                    return STATUS_OK;
                }
            } else {
                fclose(fr);
            }
        }
        if (fs && !fr) fclose(fs);
    }

    /* (c) Offline mode */
    if (offline) return STATUS_ERR_NOT_FOUND;

    /* (d) Fetch from Overpass */
    char bbox[64];
    bbox_str(bbox, sizeof(bbox), c);

    char roads_query[512];
    snprintf(roads_query, sizeof(roads_query), OVERPASS_ROADS_TMPL, bbox);

    char *roads_body = NULL;
    Status st = osm_fetch_raw(c, roads_query, &roads_body);
    if (st != STATUS_OK) return st;

    sleep(2);

    char stops_query[256];
    snprintf(stops_query, sizeof(stops_query), OVERPASS_STOPS_TMPL, bbox);

    char *stops_body = NULL;
    osm_fetch_raw(c, stops_query, &stops_body); /* failure is OK */

    /* Save raw files */
    FILE *fr = fopen(roads_path, "w");
    if (fr) { fputs(roads_body, fr); fclose(fr); }

    if (stops_body) {
        FILE *fs = fopen(stops_path, "w");
        if (fs) { fputs(stops_body, fs); fclose(fs); }
    }

    st = osm_build_graph_from_json(roads_body, stops_body, out, stats);
    free(roads_body);
    free(stops_body);

    if (st == STATUS_OK) {
        graph_save(*out, cache_path);
    }
    return st;
}
