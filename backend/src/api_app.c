#include "api_app.h"
#include "api_platform.h"
#include "ai_inference.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __has_include
#  if __has_include(<cjson/cJSON.h>)
#    include <cjson/cJSON.h>
#  else
#    include <cJSON.h>
#  endif
#else
#  include <cjson/cJSON.h>
#endif

struct AppCtx {
    AppOptions opt;
    char *bind_addr_copy;
    char *data_dir_copy;
    char *predictor_opt_copy;
    char *cors_origin_copy;

    Graph *graph;
    DemandStop *stops;
    size_t n_stops;
    NetworkModel *net;
    DemandPredictor *pred_ma;
    DemandPredictor *pred_rf;
    const DemandPredictor *active_pred;
    const char *active_pred_name;

    SimConfig sim_cfg;
    Sim *sim;
    Registry *registry;

    char *ml_metrics_json;

    ApiMutex *mutex;
    ApiThread *sim_thread;
    atomic_int stop_flag;
    int thread_running;

    int running;
    double speed;
    uint64_t geometry_version;
    uint64_t generation;
};

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len + 1);
    return copy;
}

static const char *resolve_default_data_dir(const char *override_dir) {
    if (override_dir && override_dir[0] != '\0') return override_dir;
    if (api_dir_exists("data")) return "data";
    if (api_dir_exists("../data")) return "../data";
    return "data";
}

static char *read_file_text_binary(const char *path) {
    if (!path) return NULL;
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long sz = ftell(fp);
    if (sz < 0 || sz > 1048576L) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    size_t nread = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[nread] = '\0';
    /* Validate JSON structure once at load */
    cJSON *parsed = cJSON_Parse(buf);
    if (!parsed) {
        free(buf);
        return NULL;
    }
    cJSON_Delete(parsed);
    return buf;
}

static size_t find_nearest_stop(const DemandStop *stops, size_t n_stops, double lat, double lng) {
    size_t best_idx = 0;
    double best_d = 1e30;
    for (size_t i = 0; i < n_stops; i++) {
        double d = graph_haversine_m(stops[i].lat, stops[i].lng, lat, lng);
        if (d < best_d) {
            best_d = d;
            best_idx = i;
        }
    }
    return best_idx;
}

static Status init_registry_and_timetable(AppCtx *ctx) {
    static const double HUB_MAJESTIC_LAT = 12.9767;
    static const double HUB_MAJESTIC_LNG = 77.5713;
    static const double HUB_SHIVAJINAGAR_LAT = 12.9857;
    static const double HUB_SHIVAJINAGAR_LNG = 77.6057;

    RegistryDepotSpec depots[2];
    depots[0].name = "Majestic (Kempegowda BS)";
    depots[0].stop_idx = find_nearest_stop(ctx->stops, ctx->n_stops, HUB_MAJESTIC_LAT, HUB_MAJESTIC_LNG);
    depots[1].name = "Shivajinagar";
    depots[1].stop_idx = find_nearest_stop(ctx->stops, ctx->n_stops, HUB_SHIVAJINAGAR_LAT, HUB_SHIVAJINAGAR_LNG);

    Status st = registry_create(ctx->net, depots, 2, ctx->sim, &ctx->registry);
    if (st != STATUS_OK) return st;

    st = sim_set_loop_observer(ctx->sim, registry_sim_observer, ctx->registry);
    if (st != STATUS_OK) return st;

    st = registry_sync(ctx->registry, ctx->sim);
    if (st != STATUS_OK) return st;

    size_t n_routes = network_route_count(ctx->net);
    int *counts = (int *)calloc(n_routes, sizeof(int));
    if (!counts) return STATUS_ERR_NOMEM;
    for (size_t r = 0; r < n_routes; r++) {
        counts[r] = (int)sim_route_bus_count(ctx->sim, r);
    }

    int from_m = (int)floor(ctx->sim_cfg.start_min);
    int to_m = (int)floor(ctx->sim_cfg.end_min);
    if (from_m < 0) from_m = 0;
    if (to_m > 1440) to_m = 1440;
    if (from_m < to_m) {
        st = registry_build_timetable(ctx->registry, counts, n_routes, from_m, to_m);
    }
    free(counts);
    return st;
}

static void *sim_thread_entry(void *arg) {
    AppCtx *ctx = (AppCtx *)arg;
    double last_wall = api_monotonic_s();

    for (;;) {
        api_sleep_ms(50);
        double now_wall = api_monotonic_s();
        double wall_dt = now_wall - last_wall;
        last_wall = now_wall;
        if (wall_dt < 0.0) wall_dt = 0.0;
        if (wall_dt > 0.5) wall_dt = 0.5;

        if (atomic_load(&ctx->stop_flag) != 0) {
            break;
        }

        if (app_lock(ctx) == STATUS_OK) {
            if (atomic_load(&ctx->stop_flag) != 0) {
                app_unlock(ctx);
                break;
            }
            if (ctx->running && ctx->sim && !sim_finished(ctx->sim)) {
                double cur_min = sim_now_min(ctx->sim);
                double target_min = cur_min + (ctx->speed * wall_dt) / 60.0;
                if (target_min > ctx->sim_cfg.end_min) {
                    target_min = ctx->sim_cfg.end_min;
                }
                if (target_min > cur_min) {
                    if (sim_run_until(ctx->sim, target_min) == STATUS_OK) {
                        if (ctx->registry) {
                            registry_sync(ctx->registry, ctx->sim);
                        }
                        ctx->generation++;
                    }
                }
                if (sim_finished(ctx->sim)) {
                    ctx->running = 0;
                }
            }
            app_unlock(ctx);
        }
    }
    return NULL;
}

void app_options_default(AppOptions *opt) {
    if (!opt) return;
    memset(opt, 0, sizeof(*opt));
    opt->port = 8080;
    opt->bind_addr = "127.0.0.1";
    opt->data_dir = NULL;
    opt->seed = 42ULL;
    opt->n_stops = 60;
    opt->n_routes = 6;
    opt->n_buses = 36;
    opt->n_reserve = 3;
    opt->policy = SIM_POLICY_FIXED_EQUAL;
    opt->predictor = "ma";
    opt->speed = 60.0;
    opt->paused = 0;
    opt->cors_origin = "http://localhost:5173";
}

Status app_create(const AppOptions *opt, AppCtx **out) {
    if (!out) return STATUS_ERR_NULL;
    *out = NULL;

    AppOptions local_opt;
    app_options_default(&local_opt);
    if (opt) {
        if (opt->port > 0) local_opt.port = opt->port;
        if (opt->bind_addr && opt->bind_addr[0]) local_opt.bind_addr = opt->bind_addr;
        if (opt->data_dir && opt->data_dir[0]) local_opt.data_dir = opt->data_dir;
        local_opt.seed = opt->seed;
        if (opt->n_stops > 0) local_opt.n_stops = opt->n_stops;
        if (opt->n_routes > 0) local_opt.n_routes = opt->n_routes;
        if (opt->n_buses > 0) local_opt.n_buses = opt->n_buses;
        local_opt.n_reserve = opt->n_reserve;
        local_opt.policy = opt->policy;
        if (opt->predictor && opt->predictor[0]) local_opt.predictor = opt->predictor;
        if (isfinite(opt->speed) && opt->speed > 0.0) local_opt.speed = opt->speed;
        local_opt.paused = opt->paused ? 1 : 0;
        if (opt->cors_origin && opt->cors_origin[0]) local_opt.cors_origin = opt->cors_origin;
    }

    if (local_opt.port <= 0 || local_opt.port > 65535 ||
        local_opt.n_stops < 2 || local_opt.n_routes == 0 ||
        local_opt.n_buses == 0 || local_opt.speed < 1.0 || local_opt.speed > 3600.0) {
        return STATUS_ERR_INVALID;
    }
    if (strcmp(local_opt.predictor, "oracle") != 0 &&
        strcmp(local_opt.predictor, "ma") != 0 &&
        strcmp(local_opt.predictor, "rf") != 0) {
        return STATUS_ERR_INVALID;
    }

    const char *data_dir = resolve_default_data_dir(local_opt.data_dir);

    AppCtx *ctx = (AppCtx *)calloc(1, sizeof(AppCtx));
    if (!ctx) return STATUS_ERR_NOMEM;

    ctx->opt = local_opt;
    ctx->bind_addr_copy = dup_str(local_opt.bind_addr);
    ctx->data_dir_copy = dup_str(data_dir);
    ctx->predictor_opt_copy = dup_str(local_opt.predictor);
    ctx->cors_origin_copy = dup_str(local_opt.cors_origin);
    ctx->opt.bind_addr = ctx->bind_addr_copy;
    ctx->opt.data_dir = ctx->data_dir_copy;
    ctx->opt.predictor = ctx->predictor_opt_copy;
    ctx->opt.cors_origin = ctx->cors_origin_copy;

    if (!ctx->bind_addr_copy || !ctx->data_dir_copy ||
        !ctx->predictor_opt_copy || !ctx->cors_origin_copy) {
        app_destroy(ctx);
        return STATUS_ERR_NOMEM;
    }

    ctx->mutex = api_mutex_create();
    if (!ctx->mutex) {
        app_destroy(ctx);
        return STATUS_ERR_NOMEM;
    }
    atomic_init(&ctx->stop_flag, 0);
    ctx->running = local_opt.paused ? 0 : 1;
    ctx->speed = local_opt.speed;
    ctx->geometry_version = 1;
    ctx->generation = 1;

    char cache_path[512];
    snprintf(cache_path, sizeof(cache_path), "%s/graph_cache.txt", data_dir);
    Status st = graph_load(&ctx->graph, cache_path);
    if (st != STATUS_OK || !ctx->graph) {
        fprintf(stderr, "[app] graph_cache load failed (%s)\n", status_to_string(st));
        app_destroy(ctx);
        return (st == STATUS_OK) ? STATUS_ERR_NOT_FOUND : st;
    }

    st = demand_stops_from_graph_anchored(ctx->graph, local_opt.n_stops, local_opt.seed,
                                          &ctx->stops, &ctx->n_stops);
    if (st != STATUS_OK || !ctx->stops || ctx->n_stops < 2) {
        fprintf(stderr, "[app] stop selection failed (%s)\n", status_to_string(st));
        app_destroy(ctx);
        return (st == STATUS_OK) ? STATUS_ERR_INVALID : st;
    }
    ctx->opt.n_stops = ctx->n_stops;

    NetworkConfig ncfg;
    network_config_default(&ncfg);
    ncfg.n_routes = local_opt.n_routes;
    ncfg.build_matrix = 1;
    st = network_build(ctx->graph, ctx->stops, ctx->n_stops, &ncfg, &ctx->net);
    if (st != STATUS_OK || !ctx->net) {
        fprintf(stderr, "[app] network_build failed (%s)\n", status_to_string(st));
        app_destroy(ctx);
        return (st == STATUS_OK) ? STATUS_ERR_INVALID : st;
    }

    /* Optional predictors (each optional and visibly reported) */
    char train_path[512];
    snprintf(train_path, sizeof(train_path), "%s/demand_train.csv", data_dir);
    DemandRow *train_rows = NULL;
    size_t n_train = 0;
    Status ma_st = demand_rows_load_csv(train_path, &train_rows, &n_train);
    if (ma_st == STATUS_OK && train_rows && n_train > 0) {
        ctx->pred_ma = dp_train_moving_avg(train_rows, n_train);
    }
    free(train_rows);
    fprintf(stderr, "[app] predictor ma: %s\n", ctx->pred_ma ? "loaded" : "not available");

    char rf_path[512];
    snprintf(rf_path, sizeof(rf_path), "%s/demand_rf.txt", data_dir);
    ForestModel *fm = NULL;
    if (forest_load(&fm, rf_path) == STATUS_OK && fm) {
        ctx->pred_rf = dp_from_forest(fm);
        if (!ctx->pred_rf) {
            forest_destroy(fm);
        }
    }
    fprintf(stderr, "[app] predictor rf: %s\n", ctx->pred_rf ? "loaded" : "not available");

    if (strcmp(local_opt.predictor, "rf") == 0 && ctx->pred_rf) {
        ctx->active_pred = ctx->pred_rf;
        ctx->active_pred_name = "rf";
    } else if (strcmp(local_opt.predictor, "ma") == 0 && ctx->pred_ma) {
        ctx->active_pred = ctx->pred_ma;
        ctx->active_pred_name = "ma";
    } else {
        if (strcmp(local_opt.predictor, "oracle") != 0) {
            fprintf(stderr, "[app] requested predictor '%s' unavailable, using oracle\n",
                    local_opt.predictor);
        }
        ctx->active_pred = NULL;
        ctx->active_pred_name = "oracle";
    }

    char ml_path[512];
    snprintf(ml_path, sizeof(ml_path), "%s/ml_metrics.json", data_dir);
    ctx->ml_metrics_json = read_file_text_binary(ml_path);

    sim_config_default(&ctx->sim_cfg);
    ctx->sim_cfg.seed = local_opt.seed;
    ctx->sim_cfg.n_buses = (int)local_opt.n_buses;
    ctx->sim_cfg.n_reserve = (int)local_opt.n_reserve;
    ctx->sim_cfg.policy = local_opt.policy;
    ctx->sim_cfg.predictor = ctx->active_pred;
    ctx->sim_cfg.initial_spacing = SIM_SPACING_BY_TIME;
    ctx->sim_cfg.reassign_mode = SIM_REASSIGN_DRAIN;
    ctx->sim_cfg.apply_traffic = 1;

    double *prop_weights = NULL;
    if (ctx->sim_cfg.policy == SIM_POLICY_FIXED_PROPORTIONAL) {
        size_t nr = network_route_count(ctx->net);
        prop_weights = (double *)calloc(nr, sizeof(double));
        if (!prop_weights) {
            app_destroy(ctx);
            return STATUS_ERR_NOMEM;
        }
        int start_hr = (int)floor(ctx->sim_cfg.start_min / 60.0);
        if (start_hr < 0) start_hr = 0;
        if (start_hr > 23) start_hr = 23;
        st = network_route_demands(ctx->net, ctx->active_pred, start_hr, ctx->sim_cfg.day_type,
                                   ctx->sim_cfg.rain, NULL, 0, prop_weights);
        if (st != STATUS_OK) {
            free(prop_weights);
            app_destroy(ctx);
            return st;
        }
        ctx->sim_cfg.prop_weights = prop_weights;
    }

    st = sim_create(ctx->net, &ctx->sim_cfg, &ctx->sim);
    ctx->sim_cfg.prop_weights = NULL;
    free(prop_weights);
    if (st != STATUS_OK || !ctx->sim) {
        fprintf(stderr, "[app] sim_create failed (%s)\n", status_to_string(st));
        app_destroy(ctx);
        return (st == STATUS_OK) ? STATUS_ERR_INVALID : st;
    }

    st = init_registry_and_timetable(ctx);
    if (st != STATUS_OK) {
        fprintf(stderr, "[app] registry init failed (%s)\n", status_to_string(st));
        app_destroy(ctx);
        return st;
    }

    *out = ctx;
    return STATUS_OK;
}

Status app_create_from_parts(Graph *g, NetworkModel *net, const DemandStop *stops, size_t n,
                             const SimConfig *cfg, AppCtx **out) {
    if (!g || !net || !stops || !out) return STATUS_ERR_NULL;
    *out = NULL;
    if (n < 2 || n != network_stop_count(net)) return STATUS_ERR_INVALID;

    AppCtx *ctx = (AppCtx *)calloc(1, sizeof(AppCtx));
    if (!ctx) return STATUS_ERR_NOMEM;

    app_options_default(&ctx->opt);
    ctx->opt.n_stops = n;
    ctx->opt.n_routes = network_route_count(net);
    ctx->opt.paused = 1;
    ctx->opt.predictor = "oracle";
    ctx->bind_addr_copy = dup_str("127.0.0.1");
    ctx->data_dir_copy = dup_str("data");
    ctx->predictor_opt_copy = dup_str("oracle");
    ctx->cors_origin_copy = dup_str("http://localhost:5173");
    ctx->stops = (DemandStop *)malloc(n * sizeof(DemandStop));
    ctx->mutex = api_mutex_create();

    if (!ctx->bind_addr_copy || !ctx->data_dir_copy || !ctx->predictor_opt_copy ||
        !ctx->cors_origin_copy || !ctx->stops || !ctx->mutex) {
        /* Do not free caller g/net before ownership is finalized */
        free(ctx->bind_addr_copy);
        free(ctx->data_dir_copy);
        free(ctx->predictor_opt_copy);
        free(ctx->cors_origin_copy);
        free(ctx->stops);
        api_mutex_destroy(ctx->mutex);
        free(ctx);
        return STATUS_ERR_NOMEM;
    }

    memcpy(ctx->stops, stops, n * sizeof(DemandStop));
    ctx->n_stops = n;
    ctx->opt.bind_addr = ctx->bind_addr_copy;
    ctx->opt.data_dir = ctx->data_dir_copy;
    ctx->opt.predictor = ctx->predictor_opt_copy;
    ctx->opt.cors_origin = ctx->cors_origin_copy;

    atomic_init(&ctx->stop_flag, 0);
    ctx->running = 0;
    ctx->speed = 60.0;
    ctx->geometry_version = 1;
    ctx->generation = 1;
    ctx->active_pred = NULL;
    ctx->active_pred_name = "oracle";

    if (cfg) {
        ctx->sim_cfg = *cfg;
    } else {
        sim_config_default(&ctx->sim_cfg);
        ctx->sim_cfg.initial_spacing = SIM_SPACING_BY_TIME;
        ctx->sim_cfg.reassign_mode = SIM_REASSIGN_DRAIN;
    }
    ctx->opt.seed = ctx->sim_cfg.seed;
    ctx->opt.n_buses = (ctx->sim_cfg.n_buses > 0) ? (size_t)ctx->sim_cfg.n_buses : 0U;
    ctx->opt.n_reserve = (ctx->sim_cfg.n_reserve > 0) ? (size_t)ctx->sim_cfg.n_reserve : 0U;
    ctx->opt.policy = ctx->sim_cfg.policy;

    Status st = sim_create(net, &ctx->sim_cfg, &ctx->sim);
    ctx->sim_cfg.prop_weights = NULL;
    if (st != STATUS_OK || !ctx->sim) {
        free(ctx->bind_addr_copy);
        free(ctx->data_dir_copy);
        free(ctx->predictor_opt_copy);
        free(ctx->cors_origin_copy);
        free(ctx->stops);
        api_mutex_destroy(ctx->mutex);
        free(ctx);
        return (st == STATUS_OK) ? STATUS_ERR_INVALID : st;
    }

    /* Set borrowed refs transiently; only claim ownership once registry succeeds */
    ctx->graph = g;
    ctx->net = net;
    st = init_registry_and_timetable(ctx);
    if (st != STATUS_OK) {
        ctx->graph = NULL;
        ctx->net = NULL;
        sim_destroy(ctx->sim);
        ctx->sim = NULL;
        app_destroy(ctx);
        return st;
    }

    *out = ctx;
    return STATUS_OK;
}

void app_destroy(AppCtx *ctx) {
    if (!ctx) return;
    app_stop_thread(ctx);

    registry_destroy(ctx->registry);
    ctx->registry = NULL;
    sim_destroy(ctx->sim);
    ctx->sim = NULL;
    network_destroy(ctx->net);
    ctx->net = NULL;
    free(ctx->stops);
    ctx->stops = NULL;
    graph_destroy(ctx->graph);
    ctx->graph = NULL;

    dp_destroy(ctx->pred_ma);
    ctx->pred_ma = NULL;
    dp_destroy(ctx->pred_rf);
    ctx->pred_rf = NULL;
    ctx->active_pred = NULL;

    free(ctx->ml_metrics_json);
    free(ctx->bind_addr_copy);
    free(ctx->data_dir_copy);
    free(ctx->predictor_opt_copy);
    free(ctx->cors_origin_copy);

    api_mutex_destroy(ctx->mutex);
    free(ctx);
}

Status app_lock(AppCtx *ctx) {
    if (!ctx) return STATUS_ERR_NULL;
    return api_mutex_lock(ctx->mutex);
}

Status app_unlock(AppCtx *ctx) {
    if (!ctx) return STATUS_ERR_NULL;
    return api_mutex_unlock(ctx->mutex);
}

Status app_start_thread(AppCtx *ctx) {
    if (!ctx) return STATUS_ERR_NULL;
    if (ctx->thread_running) return STATUS_OK;
    atomic_store(&ctx->stop_flag, 0);
    Status st = api_thread_create(sim_thread_entry, ctx, &ctx->sim_thread);
    if (st == STATUS_OK) {
        ctx->thread_running = 1;
    }
    return st;
}

Status app_stop_thread(AppCtx *ctx) {
    if (!ctx) return STATUS_OK;
    if (!ctx->thread_running) return STATUS_OK;
    atomic_store(&ctx->stop_flag, 1);
    Status st = api_thread_join(ctx->sim_thread);
    ctx->sim_thread = NULL;
    ctx->thread_running = 0;
    return st;
}

Status app_sim_control(AppCtx *ctx, const char *action, int has_speed, double speed) {
    if (!ctx) return STATUS_ERR_NULL;
    if (!action && !has_speed) return STATUS_ERR_INVALID;

    if (has_speed) {
        if (!isfinite(speed) || speed < 1.0 || speed > 3600.0) {
            return STATUS_ERR_INVALID;
        }
    }

    if (action) {
        if (strcmp(action, "start") == 0 || strcmp(action, "resume") == 0) {
            ctx->running = (ctx->sim && !sim_finished(ctx->sim)) ? 1 : 0;
        } else if (strcmp(action, "pause") == 0) {
            ctx->running = 0;
        } else if (strcmp(action, "speed") == 0) {
            if (!has_speed) return STATUS_ERR_INVALID;
        } else {
            return STATUS_ERR_INVALID;
        }
    }

    if (has_speed) {
        ctx->speed = speed;
    }
    ctx->generation++;
    return STATUS_OK;
}

void app_bump_geometry_version(AppCtx *ctx) {
    if (!ctx) return;
    ctx->geometry_version++;
    ctx->generation++;
}

const AppOptions *app_options(const AppCtx *ctx) { return ctx ? &ctx->opt : NULL; }
const Graph *app_graph(const AppCtx *ctx) { return ctx ? ctx->graph : NULL; }
const NetworkModel *app_network(const AppCtx *ctx) { return ctx ? ctx->net : NULL; }
const DemandStop *app_stops(const AppCtx *ctx, size_t *n_out) {
    if (n_out) *n_out = ctx ? ctx->n_stops : 0;
    return ctx ? ctx->stops : NULL;
}
Sim *app_sim(AppCtx *ctx) { return ctx ? ctx->sim : NULL; }
const Sim *app_sim_const(const AppCtx *ctx) { return ctx ? ctx->sim : NULL; }
Registry *app_registry(AppCtx *ctx) { return ctx ? ctx->registry : NULL; }
const Registry *app_registry_const(const AppCtx *ctx) { return ctx ? ctx->registry : NULL; }
const SimConfig *app_sim_config(const AppCtx *ctx) { return ctx ? &ctx->sim_cfg : NULL; }
const DemandPredictor *app_predictor_ma(const AppCtx *ctx) { return ctx ? ctx->pred_ma : NULL; }
const DemandPredictor *app_predictor_rf(const AppCtx *ctx) { return ctx ? ctx->pred_rf : NULL; }
const DemandPredictor *app_active_predictor(const AppCtx *ctx) { return ctx ? ctx->active_pred : NULL; }
const char *app_active_predictor_name(const AppCtx *ctx) {
    return (ctx && ctx->active_pred_name) ? ctx->active_pred_name : "oracle";
}
const char *app_ml_metrics_json(const AppCtx *ctx) { return ctx ? ctx->ml_metrics_json : NULL; }
int app_is_running(const AppCtx *ctx) { return ctx ? ctx->running : 0; }
double app_speed(const AppCtx *ctx) { return ctx ? ctx->speed : 0.0; }
uint64_t app_geometry_version(const AppCtx *ctx) { return ctx ? ctx->geometry_version : 0ULL; }
uint64_t app_generation(const AppCtx *ctx) { return ctx ? ctx->generation : 0ULL; }
const char *app_cors_origin(const AppCtx *ctx) {
    return (ctx && ctx->opt.cors_origin) ? ctx->opt.cors_origin : "http://localhost:5173";
}

