#define _POSIX_C_SOURCE 200809L

#include "traffic_client.h"
#include "traffic_sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

/* cJSON may be in different locations */
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

/* ---- TomTom API constants ---- */
static const char *TOMTOM_BASE_URL = "https://api.tomtom.com";
static const char *TOMTOM_SERVICE  = "traffic/services";
static const int   TOMTOM_VERSION  = 4;
static const char *TOMTOM_STYLE    = "absolute";
static const int   TOMTOM_ZOOM     = 10;
static const char *TOMTOM_FORMAT   = "json";
static const char *TOMTOM_UNIT     = "KMPH";
static const char *TOMTOM_USER_AGENT = "SmartTransportStudentProject/1.0";
static const size_t TOMTOM_RESP_CAP  = 256 * 1024; /* 256 KB */
static const long   TOMTOM_CONNECT_TIMEOUT = 3L;
static const long   TOMTOM_TOTAL_TIMEOUT   = 6L;

/* ---- .env loader ---- */

static int is_valid_name_char(char c, int first) {
    if (first) return isalpha((unsigned char)c) || c == '_';
    return isalnum((unsigned char)c) || c == '_';
}

Status env_load_file(const char *path) {
    if (!path) return STATUS_ERR_NULL;
    FILE *f = fopen(path, "r");
    if (!f) return STATUS_ERR_NOT_FOUND;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        /* Check for over-long line (no newline found and buffer full) */
        size_t len = strlen(line);
        if (len == sizeof(line) - 1 && line[len - 1] != '\n') {
            /* Check if we're not at EOF */
            int c = fgetc(f);
            if (c != EOF) {
                fclose(f);
                return STATUS_ERR_INVALID;
            }
        }

        /* Strip trailing \r\n */
        while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) {
            line[--len] = '\0';
        }

        char *p = line;

        /* Skip leading spaces */
        while (*p == ' ' || *p == '\t') p++;

        /* Skip blank lines and comments */
        if (*p == '\0' || *p == '#') continue;

        /* Strip optional "export " prefix */
        if (strncmp(p, "export ", 7) == 0) {
            p += 7;
            while (*p == ' ' || *p == '\t') p++;
        }

        /* Parse name */
        char name[256];
        size_t ni = 0;
        if (!is_valid_name_char(*p, 1)) { fclose(f); return STATUS_ERR_INVALID; }
        while (*p && is_valid_name_char(*p, 0)) {
            if (ni < sizeof(name) - 1) name[ni++] = *p;
            p++;
        }
        name[ni] = '\0';

        /* Skip spaces, expect '=' */
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '=') { fclose(f); return STATUS_ERR_INVALID; }
        p++;
        while (*p == ' ' || *p == '\t') p++;

        /* Parse value: strip one pair of matching quotes */
        char value[1024];
        size_t vlen = strlen(p);
        char *v = p;
        if ((v[0] == '"' && vlen >= 2 && v[vlen-1] == '"') ||
            (v[0] == '\'' && vlen >= 2 && v[vlen-1] == '\'')) {
            v[vlen-1] = '\0';
            v++;
        }

        /* Trim trailing spaces */
        size_t vl = strlen(v);
        while (vl > 0 && (v[vl-1] == ' ' || v[vl-1] == '\t')) v[--vl] = '\0';

        strncpy(value, v, sizeof(value) - 1);
        value[sizeof(value) - 1] = '\0';

        /* setenv with no-overwrite (0 means don't overwrite) */
        setenv(name, value, 0);
    }

    fclose(f);
    return STATUS_OK;
}

Status env_load_default(void) {
    Status st = env_load_file(".env");
    if (st == STATUS_OK) return STATUS_OK;
    st = env_load_file("../.env");
    if (st == STATUS_OK) return STATUS_OK;
    st = env_load_file("../../.env");
    if (st == STATUS_OK) return STATUS_OK;
    return STATUS_ERR_NOT_FOUND;
}

/* ---- Pure functions ---- */

Status tomtom_build_url(char *buf, size_t n, const char *api_key, double lat, double lng) {
    if (!buf || n == 0 || !api_key) return STATUS_ERR_NULL;
    if (api_key[0] == '\0') return STATUS_ERR_INVALID;

    int written = snprintf(buf, n,
        "%s/%s/%d/flowSegmentData/%s/%d/%s?key=%s&point=%.6f,%.6f&unit=%s",
        TOMTOM_BASE_URL, TOMTOM_SERVICE, TOMTOM_VERSION,
        TOMTOM_STYLE, TOMTOM_ZOOM, TOMTOM_FORMAT,
        api_key, lat, lng, TOMTOM_UNIT);

    if (written < 0 || (size_t)written >= n) return STATUS_ERR_INVALID;
    return STATUS_OK;
}

void tomtom_redact_url(const char *url, char *buf, size_t n) {
    if (!url || !buf || n == 0) {
        if (buf && n > 0) buf[0] = '\0';
        return;
    }

    const char *key_param = strstr(url, "key=");
    if (!key_param) {
        strncpy(buf, url, n - 1);
        buf[n - 1] = '\0';
        return;
    }

    /* Copy up to and including "key=" */
    size_t prefix_len = (size_t)(key_param - url) + 4;
    size_t copy_len = prefix_len < n - 1 ? prefix_len : n - 1;
    memcpy(buf, url, copy_len);
    buf[copy_len] = '\0';

    /* Append "***" */
    if (copy_len < n - 1) {
        strncat(buf, "***", n - 1 - copy_len);
    }

    /* Find where the key value ends (next '&' or end of string) */
    const char *key_val_start = key_param + 4;
    const char *key_val_end = strchr(key_val_start, '&');
    if (key_val_end) {
        /* Append the rest of the URL */
        size_t cur_len = strlen(buf);
        if (cur_len < n - 1) {
            strncat(buf, key_val_end, n - 1 - cur_len);
        }
    }
    buf[n - 1] = '\0';
}

Status tomtom_parse_flow_json(const char *json, TomTomFlow *out) {
    if (!json || !out) return STATUS_ERR_NULL;

    cJSON *root = cJSON_Parse(json);
    if (!root) return STATUS_ERR_INVALID;

    /* Check for error response */
    if (cJSON_GetObjectItemCaseSensitive(root, "error") ||
        cJSON_GetObjectItemCaseSensitive(root, "detailedError")) {
        cJSON_Delete(root);
        return STATUS_ERR_INVALID;
    }

    cJSON *fsd = cJSON_GetObjectItemCaseSensitive(root, "flowSegmentData");
    if (!fsd || !cJSON_IsObject(fsd)) {
        cJSON_Delete(root);
        return STATUS_ERR_INVALID;
    }

    cJSON *cur_speed = cJSON_GetObjectItemCaseSensitive(fsd, "currentSpeed");
    cJSON *ff_speed  = cJSON_GetObjectItemCaseSensitive(fsd, "freeFlowSpeed");
    if (!cur_speed || !cJSON_IsNumber(cur_speed) ||
        !ff_speed  || !cJSON_IsNumber(ff_speed)) {
        cJSON_Delete(root);
        return STATUS_ERR_INVALID;
    }

    out->current_speed    = cJSON_GetNumberValue(cur_speed);
    out->free_flow_speed  = cJSON_GetNumberValue(ff_speed);

    cJSON *conf = cJSON_GetObjectItemCaseSensitive(fsd, "confidence");
    out->confidence = (conf && cJSON_IsNumber(conf)) ? cJSON_GetNumberValue(conf) : 1.0;

    cJSON *closure = cJSON_GetObjectItemCaseSensitive(fsd, "roadClosure");
    out->road_closure = (closure && cJSON_IsBool(closure)) ? (cJSON_IsTrue(closure) ? 1 : 0) : 0;

    cJSON_Delete(root);
    return STATUS_OK;
}

double tomtom_traffic_factor(const TomTomFlow *f) {
    if (!f) return GRAPH_MAX_TRAFFIC_FACTOR;
    if (f->road_closure || f->current_speed <= 0.0) return GRAPH_MAX_TRAFFIC_FACTOR;
    double ratio = f->free_flow_speed / f->current_speed;
    if (ratio < 1.0) ratio = 1.0;
    if (ratio > GRAPH_MAX_TRAFFIC_FACTOR) ratio = GRAPH_MAX_TRAFFIC_FACTOR;
    return ratio;
}

/* ---- libcurl HTTP fetch ---- */

typedef struct CurlBuf {
    char *data;
    size_t size;
    size_t cap;
} CurlBuf;

static size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    CurlBuf *buf = (CurlBuf *)userdata;
    size_t incoming = size * nmemb;
    if (buf->size + incoming > TOMTOM_RESP_CAP) return 0; /* cap exceeded */
    if (buf->size + incoming + 1 > buf->cap) {
        size_t nc = buf->cap == 0 ? 4096 : buf->cap * 2;
        while (nc < buf->size + incoming + 1) nc *= 2;
        char *nd = (char *)realloc(buf->data, nc);
        if (!nd) return 0;
        buf->data = nd;
        buf->cap = nc;
    }
    memcpy(buf->data + buf->size, ptr, incoming);
    buf->size += incoming;
    buf->data[buf->size] = '\0';
    return incoming;
}

static Status default_http_fetch(const char *url, char **body_out, long *http_status, void *ctx) {
    (void)ctx;
    if (!url || !body_out || !http_status) return STATUS_ERR_NULL;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    if (!curl) { curl_global_cleanup(); return STATUS_ERR_INVALID; }

    CurlBuf buf = {NULL, 0, 0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, TOMTOM_CONNECT_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, TOMTOM_TOTAL_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, TOMTOM_USER_AGENT);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, http_status);
    } else {
        *http_status = 0;
    }

    curl_easy_cleanup(curl);
    curl_global_cleanup();

    if (res != CURLE_OK) {
        free(buf.data);
        return STATUS_ERR_INVALID;
    }

    *body_out = buf.data;
    return STATUS_OK;
}

/* ---- TrafficUpdater ---- */

typedef struct CorridorState {
    int live;           /* 1 if currently has a usable live value */
    double fetched_at;  /* now_s when last successfully fetched */
    double factor;      /* current factor (from live or simulated) */
} CorridorState;

struct TrafficUpdater {
    Graph *g;
    TrafficCorridor *corridors;
    size_t n_corridors;
    CorridorState *corridor_states;

    char api_key[256];
    int max_calls;
    int cache_seconds;
    int offline;
    HttpFetchFn fetch;
    void *fetch_ctx;

    int call_count;
    char last_error[256];
    TrafficSource source;
};

TrafficUpdater *tu_create(Graph *g, const TrafficCorridor *corridors, size_t n, const TrafficConfig *cfg) {
    if (!g) return NULL;

    TrafficUpdater *tu = (TrafficUpdater *)calloc(1, sizeof(TrafficUpdater));
    if (!tu) return NULL;

    tu->g = g;
    tu->n_corridors = n;

    if (n > 0 && corridors) {
        tu->corridors = (TrafficCorridor *)malloc(n * sizeof(TrafficCorridor));
        tu->corridor_states = (CorridorState *)calloc(n, sizeof(CorridorState));
        if (!tu->corridors || !tu->corridor_states) {
            free(tu->corridors);
            free(tu->corridor_states);
            free(tu);
            return NULL;
        }
        memcpy(tu->corridors, corridors, n * sizeof(TrafficCorridor));
    }

    tu->max_calls    = (cfg && cfg->max_calls    > 0) ? cfg->max_calls    : 500;
    tu->cache_seconds = (cfg && cfg->cache_seconds > 0) ? cfg->cache_seconds : 300;
    tu->offline      = (cfg && cfg->offline) ? 1 : 0;
    tu->fetch        = (cfg && cfg->fetch) ? cfg->fetch : default_http_fetch;
    tu->fetch_ctx    = (cfg) ? cfg->fetch_ctx : NULL;

    if (cfg && cfg->api_key && cfg->api_key[0]) {
        strncpy(tu->api_key, cfg->api_key, sizeof(tu->api_key) - 1);
    } else {
        const char *env_key = getenv("TOMTOM_API_KEY");
        if (env_key) strncpy(tu->api_key, env_key, sizeof(tu->api_key) - 1);
    }
    tu->api_key[sizeof(tu->api_key) - 1] = '\0';

    tu->source = TRAFFIC_SIMULATED;
    return tu;
}

void tu_destroy(TrafficUpdater *tu) {
    if (!tu) return;
    free(tu->corridors);
    free(tu->corridor_states);
    free(tu);
}

Status tu_update(TrafficUpdater *tu, double now_s, double sim_minutes, int rain) {
    if (!tu) return STATUS_ERR_NULL;

    /* Step 1: Always apply simulated base layer */
    traffic_sim_apply(tu->g, sim_minutes, rain);

    int any_live = 0;

    /* Step 2: Per-corridor live fetch (if conditions allow) */
    int can_fetch = !tu->offline && tu->api_key[0] != '\0' && tu->call_count < tu->max_calls;

    for (size_t i = 0; i < tu->n_corridors; i++) {
        CorridorState *cs = &tu->corridor_states[i];

        if (!can_fetch) {
            cs->live = 0;
            continue;
        }

        /* Use cache if fresh */
        if (cs->live && (now_s - cs->fetched_at) < (double)tu->cache_seconds) {
            any_live = 1;
            /* Apply cached factor to edges near corridor */
            int near_ids[256];
            size_t near_count = graph_find_edges_near(tu->g,
                tu->corridors[i].lat, tu->corridors[i].lng,
                tu->corridors[i].radius_m, near_ids, 256);
            for (size_t ei = 0; ei < near_count; ei++) {
                graph_set_traffic_factor(tu->g, near_ids[ei], cs->factor);
            }
            continue;
        }

        /* Fetch */
        char url[512];
        Status st = tomtom_build_url(url, sizeof(url), tu->api_key,
                                      tu->corridors[i].lat, tu->corridors[i].lng);
        if (st != STATUS_OK) {
            cs->live = 0;
            snprintf(tu->last_error, sizeof(tu->last_error), "URL build failed for corridor %zu", i);
            continue;
        }

        char *body = NULL;
        long http_status = 0;
        st = tu->fetch(url, &body, &http_status, tu->fetch_ctx);
        tu->call_count++;
        can_fetch = (tu->call_count < tu->max_calls);

        if (st != STATUS_OK || http_status != 200) {
            free(body);
            cs->live = 0;
            char redacted[512];
            tomtom_redact_url(url, redacted, sizeof(redacted));
            snprintf(tu->last_error, sizeof(tu->last_error),
                     "Fetch failed: HTTP %ld for corridor %zu", http_status, i);
            continue;
        }

        TomTomFlow flow;
        st = tomtom_parse_flow_json(body, &flow);
        free(body);

        if (st != STATUS_OK || flow.confidence < 0.4) {
            cs->live = 0;
            snprintf(tu->last_error, sizeof(tu->last_error),
                     "Parse/confidence fail for corridor %zu (conf=%.2f)", i, flow.confidence);
            continue;
        }

        double factor = tomtom_traffic_factor(&flow);
        cs->live = 1;
        cs->fetched_at = now_s;
        cs->factor = factor;
        any_live = 1;

        int near_ids[256];
        size_t near_count = graph_find_edges_near(tu->g,
            tu->corridors[i].lat, tu->corridors[i].lng,
            tu->corridors[i].radius_m, near_ids, 256);
        for (size_t ei = 0; ei < near_count; ei++) {
            graph_set_traffic_factor(tu->g, near_ids[ei], factor);
        }
    }

    tu->source = any_live ? TRAFFIC_LIVE : TRAFFIC_SIMULATED;
    return STATUS_OK;
}

TrafficSource tu_source(const TrafficUpdater *tu) {
    return tu ? tu->source : TRAFFIC_SIMULATED;
}

int tu_call_count(const TrafficUpdater *tu) {
    return tu ? tu->call_count : 0;
}

const char *tu_last_error(const TrafficUpdater *tu) {
    if (!tu) return "";
    return tu->last_error;
}

size_t tu_live_corridor_count(const TrafficUpdater *tu) {
    if (!tu) return 0;
    size_t count = 0;
    for (size_t i = 0; i < tu->n_corridors; i++) {
        if (tu->corridor_states[i].live) count++;
    }
    return count;
}

Status tu_load_corridors(const char *path, TrafficCorridor *out, size_t max, size_t *count) {
    if (!path || !out || !count) return STATUS_ERR_NULL;
    *count = 0;

    FILE *f = fopen(path, "r");
    if (!f) return STATUS_ERR_NOT_FOUND;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        /* Strip CRLF */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) line[--len] = '\0';

        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#') continue;

        if (*count >= max) {
            fclose(f);
            return STATUS_ERR_INVALID;
        }

        /* Parse: name\tlat\tlng\tradius_m */
        char *tab1 = strchr(p, '\t');
        if (!tab1) { fclose(f); return STATUS_ERR_INVALID; }
        *tab1 = '\0';

        char *tab2 = strchr(tab1 + 1, '\t');
        if (!tab2) { fclose(f); return STATUS_ERR_INVALID; }
        *tab2 = '\0';

        char *tab3 = strchr(tab2 + 1, '\t');
        if (!tab3) { fclose(f); return STATUS_ERR_INVALID; }
        *tab3 = '\0';

        char *endptr = NULL;
        double lat = strtod(tab1 + 1, &endptr);
        if (*endptr != '\0') { fclose(f); return STATUS_ERR_INVALID; }

        double lng = strtod(tab2 + 1, &endptr);
        if (*endptr != '\0') { fclose(f); return STATUS_ERR_INVALID; }

        double radius_m = strtod(tab3 + 1, &endptr);
        if (*endptr != '\0') { fclose(f); return STATUS_ERR_INVALID; }

        TrafficCorridor *tc = &out[*count];
        strncpy(tc->name, p, sizeof(tc->name) - 1);
        tc->name[sizeof(tc->name) - 1] = '\0';
        tc->lat = lat;
        tc->lng = lng;
        tc->radius_m = radius_m;
        (*count)++;
    }

    fclose(f);
    return STATUS_OK;
}
