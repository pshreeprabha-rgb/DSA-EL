#include "api_app.h"
#include "api_platform.h"
#include "ai_inference.h"
#include "traffic_client.h"

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

#define API_MAX_CORRIDORS 64

struct AppCtx {
    AppOptions opt;
    char *bind_addr_copy;
    char *data_dir_copy;
    char *predictor_opt_copy;
    char *cors_origin_copy;
    char *eval_path_copy;

    Graph *graph;
    DemandStop *stops;
    size_t n_stops;
    NetworkModel *net;
    DemandPredictor *pred_ma;
    DemandPredictor *pred_rf;
    const DemandPredictor *active_pred;
    const char *active_pred_name;

    SimConfig base_sim_cfg;
    const DemandPredictor *base_active_pred;
    const char *base_active_pred_name;

    SimConfig sim_cfg;
    Sim *sim;
    Registry *registry;

    char *ml_metrics_json;
    char *last_optimize_json;

    /* Private traffic poller state (poller_graph & tu touched solely by poller thread until joined) */
    Graph *poller_graph;
    TrafficUpdater *tu;
    TrafficSource traffic_source;
    int traffic_calls;
    size_t traffic_live_corridors;
    char traffic_last_error[256];

    ApiMutex *mutex;
    ApiThread *sim_thread;
    ApiThread *poller_thread;
    atomic_int stop_flag;
    int thread_running;
    int poller_running;

    int running;
    double speed;
    size_t last_rm_history_size;
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

void app_sync_after_step(AppCtx *ctx) {
    if (!ctx) return;
    if (ctx->registry && ctx->sim) {
        registry_sync(ctx->registry, ctx->sim);
    }
    RouteManager *rm = ctx->net ? network_route_manager(ctx->net) : NULL;
    if (rm) {
        size_t hsz = rm_history_size(rm);
        if (hsz != ctx->last_rm_history_size) {
            ctx->last_rm_history_size = hsz;
            ctx->geometry_version++;
        }
    }
}

static Status create_sim_with_cfg(AppCtx *ctx, const SimConfig *cfg_in) {
    SimConfig cfg = *cfg_in;
    double *prop_weights = NULL;

    if (cfg.policy == SIM_POLICY_FIXED_PROPORTIONAL) {
        size_t nr = network_route_count(ctx->net);
        prop_weights = (double *)calloc(nr > 0 ? nr : 1, sizeof(double));
        if (!prop_weights) return STATUS_ERR_NOMEM;
        int start_hr = (int)floor(cfg.start_min / 60.0);
        if (start_hr < 0) start_hr = 0;
        if (start_hr > 23) start_hr = 23;
        Status wst = network_route_demands(ctx->net, cfg.predictor, start_hr, cfg.day_type,
                                           cfg.rain, NULL, 0, prop_weights);
        if (wst != STATUS_OK) {
            free(prop_weights);
            return wst;
        }
        cfg.prop_weights = prop_weights;
    }

    Sim *new_sim = NULL;
    Status st = sim_create(ctx->net, &cfg, &new_sim);
    free(prop_weights);
    if (st != STATUS_OK || !new_sim) {
        return (st == STATUS_OK) ? STATUS_ERR_INVALID : st;
    }
    ctx->sim = new_sim;
    return STATUS_OK;
}

static Status app_reset_locked(AppCtx *ctx) {
    if (!ctx || !ctx->net || !ctx->graph) return STATUS_ERR_NULL;

    registry_destroy(ctx->registry);
    ctx->registry = NULL;
    sim_destroy(ctx->sim);
    ctx->sim = NULL;

    free(ctx->last_optimize_json);
    ctx->last_optimize_json = NULL;

    RouteManager *rm = network_route_manager(ctx->net);
    if (rm) {
        while (rm_undo(rm, NULL, 0) == STATUS_OK) {
        }
    }

    graph_reset_traffic(ctx->graph);
    size_t n_edges = graph_edge_count(ctx->graph);
    for (size_t e = 0; e < n_edges; e++) {
        const Edge *edge = graph_edge(ctx->graph, (int)e);
        if (edge && edge->blocked) {
            graph_set_blocked(ctx->graph, (int)e, 0);
        }
    }

    ctx->sim_cfg = ctx->base_sim_cfg;
    ctx->active_pred = ctx->base_active_pred;
    ctx->active_pred_name = ctx->base_active_pred_name;

    Status st = create_sim_with_cfg(ctx, &ctx->sim_cfg);
    if (st != STATUS_OK) return st;

    st = init_registry_and_timetable(ctx);
    if (st != STATUS_OK) return st;

    ctx->running = 0;
    ctx->last_rm_history_size = 0;
    ctx->geometry_version++;
    ctx->generation++;
    return STATUS_OK;
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
                        app_sync_after_step(ctx);
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

static void *poller_thread_entry(void *arg) {
    AppCtx *ctx = (AppCtx *)arg;

    for (;;) {
        if (atomic_load(&ctx->stop_flag) != 0) break;

        double sim_min = 360.0;
        int rain = 0;
        if (app_lock(ctx) == STATUS_OK) {
            if (atomic_load(&ctx->stop_flag) != 0) {
                app_unlock(ctx);
                break;
            }
            if (ctx->sim) {
                sim_min = sim_now_min(ctx->sim);
                rain = sim_rain_active(ctx->sim);
            }
            app_unlock(ctx);
        }

        /* Execute traffic update WITHOUT holding mutex (may take seconds if live HTTP enabled) */
        if (ctx->tu && ctx->poller_graph) {
            double wall_s = api_monotonic_s();
            (void)tu_update(ctx->tu, wall_s, sim_min, rain);

            TrafficSource tsrc = tu_source(ctx->tu);
            int tcalls = tu_call_count(ctx->tu);
            size_t tlive = tu_live_corridor_count(ctx->tu);
            const char *terr = tu_last_error(ctx->tu);

            if (app_lock(ctx) == STATUS_OK) {
                if (ctx->graph && ctx->poller_graph) {
                    size_t ne = graph_edge_count(ctx->graph);
                    size_t pne = graph_edge_count(ctx->poller_graph);
                    size_t lim = (ne < pne) ? ne : pne;
                    for (size_t e = 0; e < lim; e++) {
                        const Edge *pe = graph_edge(ctx->poller_graph, (int)e);
                        if (pe) {
                            graph_set_traffic_factor(ctx->graph, (int)e, pe->traffic_factor);
                        }
                    }
                }
                ctx->traffic_source = tsrc;
                ctx->traffic_calls = tcalls;
                ctx->traffic_live_corridors = tlive;
                snprintf(ctx->traffic_last_error, sizeof(ctx->traffic_last_error),
                         "%s", terr ? terr : "");
                ctx->generation++;
                app_unlock(ctx);
            }
        }

        /* Sleep ~5 seconds in 50 ms increments for fast shutdown */
        for (int tick = 0; tick < 100; tick++) {
            if (atomic_load(&ctx->stop_flag) != 0) return NULL;
            api_sleep_ms(50);
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
    opt->offline = 0;
    opt->no_poll = 0;
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
        local_opt.offline = opt->offline ? 1 : 0;
        local_opt.no_poll = opt->no_poll ? 1 : 0;
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

    char eval_buf[512];
    snprintf(eval_buf, sizeof(eval_buf), "%s/eval/summary.json", data_dir);
    ctx->eval_path_copy = dup_str(eval_buf);

    ctx->opt.bind_addr = ctx->bind_addr_copy;
    ctx->opt.data_dir = ctx->data_dir_copy;
    ctx->opt.predictor = ctx->predictor_opt_copy;
    ctx->opt.cors_origin = ctx->cors_origin_copy;

    if (!ctx->bind_addr_copy || !ctx->data_dir_copy ||
        !ctx->predictor_opt_copy || !ctx->cors_origin_copy || !ctx->eval_path_copy) {
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
    ctx->traffic_source = TRAFFIC_SIMULATED;

    char cache_path[512];
    snprintf(cache_path, sizeof(cache_path), "%s/graph_cache.txt", data_dir);
    Status st = graph_load(&ctx->graph, cache_path);
    if (st != STATUS_OK || !ctx->graph) {
        fprintf(stderr, "[app] graph_cache load failed (%s)\n", status_to_string(st));
        app_destroy(ctx);
        return (st == STATUS_OK) ? STATUS_ERR_NOT_FOUND : st;
    }

    /* Private graph copy + TrafficUpdater for background traffic poller */
    st = graph_load(&ctx->poller_graph, cache_path);
    if (st != STATUS_OK || !ctx->poller_graph) {
        fprintf(stderr, "[app] poller graph load failed (%s)\n", status_to_string(st));
        app_destroy(ctx);
        return (st == STATUS_OK) ? STATUS_ERR_NOT_FOUND : st;
    }

    char corr_path[512];
    snprintf(corr_path, sizeof(corr_path), "%s/corridors.txt", data_dir);
    TrafficCorridor corridors[API_MAX_CORRIDORS];
    size_t n_corridors = 0;
    if (tu_load_corridors(corr_path, corridors, API_MAX_CORRIDORS, &n_corridors) != STATUS_OK) {
        n_corridors = 0;
    }
    TrafficConfig tcfg;
    memset(&tcfg, 0, sizeof(tcfg));
    tcfg.offline = (local_opt.offline || local_opt.no_poll) ? 1 : 0;
    ctx->tu = tu_create(ctx->poller_graph, corridors, n_corridors, &tcfg);
    if (!ctx->tu) {
        app_destroy(ctx);
        return STATUS_ERR_NOMEM;
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

    /* Poller manages graph traffic updates, anomaly_detect enabled */
    sim_config_default(&ctx->sim_cfg);
    ctx->sim_cfg.seed = local_opt.seed;
    ctx->sim_cfg.n_buses = (int)local_opt.n_buses;
    ctx->sim_cfg.n_reserve = (int)local_opt.n_reserve;
    ctx->sim_cfg.policy = local_opt.policy;
    ctx->sim_cfg.predictor = ctx->active_pred;
    ctx->sim_cfg.initial_spacing = SIM_SPACING_BY_TIME;
    ctx->sim_cfg.reassign_mode = SIM_REASSIGN_DRAIN;
    ctx->sim_cfg.apply_traffic = 0;
    ctx->sim_cfg.anomaly_detect = 1;

    ctx->base_sim_cfg = ctx->sim_cfg;
    ctx->base_active_pred = ctx->active_pred;
    ctx->base_active_pred_name = ctx->active_pred_name;

    st = create_sim_with_cfg(ctx, &ctx->sim_cfg);
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
    ctx->opt.offline = 1;
    ctx->bind_addr_copy = dup_str("127.0.0.1");
    ctx->data_dir_copy = dup_str("data");
    ctx->predictor_opt_copy = dup_str("oracle");
    ctx->cors_origin_copy = dup_str("http://localhost:5173");
    ctx->eval_path_copy = dup_str("data/eval/summary.json");
    ctx->stops = (DemandStop *)malloc(n * sizeof(DemandStop));
    ctx->mutex = api_mutex_create();

    if (!ctx->bind_addr_copy || !ctx->data_dir_copy || !ctx->predictor_opt_copy ||
        !ctx->cors_origin_copy || !ctx->eval_path_copy || !ctx->stops || !ctx->mutex) {
        free(ctx->bind_addr_copy);
        free(ctx->data_dir_copy);
        free(ctx->predictor_opt_copy);
        free(ctx->cors_origin_copy);
        free(ctx->eval_path_copy);
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
    ctx->traffic_source = TRAFFIC_SIMULATED;

    if (cfg) {
        ctx->sim_cfg = *cfg;
    } else {
        sim_config_default(&ctx->sim_cfg);
        ctx->sim_cfg.initial_spacing = SIM_SPACING_BY_TIME;
        ctx->sim_cfg.reassign_mode = SIM_REASSIGN_DRAIN;
    }
    ctx->active_pred = ctx->sim_cfg.predictor;
    ctx->active_pred_name = ctx->active_pred ? dp_name(ctx->active_pred) : "oracle";
    ctx->base_sim_cfg = ctx->sim_cfg;
    ctx->base_sim_cfg.prop_weights = NULL;
    ctx->base_active_pred = ctx->active_pred;
    ctx->base_active_pred_name = ctx->active_pred_name;

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
        free(ctx->eval_path_copy);
        free(ctx->stops);
        api_mutex_destroy(ctx->mutex);
        free(ctx);
        return (st == STATUS_OK) ? STATUS_ERR_INVALID : st;
    }

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

    tu_destroy(ctx->tu);
    ctx->tu = NULL;
    graph_destroy(ctx->poller_graph);
    ctx->poller_graph = NULL;

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
    free(ctx->last_optimize_json);
    free(ctx->eval_path_copy);
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
    atomic_store(&ctx->stop_flag, 0);
    if (!ctx->thread_running) {
        Status st = api_thread_create(sim_thread_entry, ctx, &ctx->sim_thread);
        if (st != STATUS_OK) return st;
        ctx->thread_running = 1;
    }
    if (ctx->tu && ctx->poller_graph && !ctx->poller_running) {
        Status st = api_thread_create(poller_thread_entry, ctx, &ctx->poller_thread);
        if (st != STATUS_OK) return st;
        ctx->poller_running = 1;
    }
    return STATUS_OK;
}

Status app_stop_thread(AppCtx *ctx) {
    if (!ctx) return STATUS_OK;
    atomic_store(&ctx->stop_flag, 1);
    Status st = STATUS_OK;
    if (ctx->thread_running) {
        Status s1 = api_thread_join(ctx->sim_thread);
        ctx->sim_thread = NULL;
        ctx->thread_running = 0;
        if (s1 != STATUS_OK) st = s1;
    }
    if (ctx->poller_running) {
        Status s2 = api_thread_join(ctx->poller_thread);
        ctx->poller_thread = NULL;
        ctx->poller_running = 0;
        if (s2 != STATUS_OK) st = s2;
    }
    return st;
}

Status app_sim_control(AppCtx *ctx, const char *action, int has_speed, double speed) {
    return app_sim_control_ext(ctx, action, has_speed, speed, 0, 0.0);
}

Status app_sim_control_ext(AppCtx *ctx, const char *action, int has_speed, double speed,
                           int has_time_min, double time_min) {
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
        } else if (strcmp(action, "reset") == 0) {
            Status st = app_reset_locked(ctx);
            if (st != STATUS_OK) return st;
        } else if (strcmp(action, "seek") == 0) {
            if (!has_time_min || !isfinite(time_min)) return STATUS_ERR_INVALID;
            double start_m = ctx->base_sim_cfg.start_min;
            double end_m = ctx->base_sim_cfg.end_min;
            double slice_m = ctx->base_sim_cfg.slice_min;
            if (time_min < start_m - 1e-9 || time_min > end_m + 1e-9) {
                return STATUS_ERR_INVALID;
            }
            double rem = fabs(time_min - round(time_min / slice_m) * slice_m);
            if (rem > 1e-6) {
                return STATUS_ERR_INVALID;
            }
            Status st = app_reset_locked(ctx);
            if (st != STATUS_OK) return st;
            if (time_min > sim_now_min(ctx->sim)) {
                st = sim_run_until(ctx->sim, time_min);
                if (st != STATUS_OK) return st;
                app_sync_after_step(ctx);
            }
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

Status app_schedule_scenario(AppCtx *ctx, SimScenarioKind kind, int arg, double *time_min_out) {
    if (!ctx || !ctx->sim) return STATUS_ERR_NULL;
    double now = sim_now_min(ctx->sim);
    double slice = ctx->sim_cfg.slice_min;
    if (slice <= 0.0) return STATUS_ERR_INVALID;

    double k = ceil((now - 1e-9) / slice);
    double t_min = k * slice;
    if (t_min < now) {
        t_min = (floor(now / slice) + 1.0) * slice;
    }
    if (t_min >= ctx->sim_cfg.end_min - 1e-9) {
        return STATUS_ERR_EMPTY;
    }

    SimScenario sc;
    sc.time_min = t_min;
    sc.kind = kind;
    sc.arg = arg;
    Status st = sim_schedule(ctx->sim, &sc);
    if (st != STATUS_OK) return st;

    app_sync_after_step(ctx);
    ctx->generation++;
    if (time_min_out) *time_min_out = t_min;
    return STATUS_OK;
}

Status app_set_allocation(AppCtx *ctx, SimPolicy policy, const char *pred_name,
                          int force, size_t *moves_applied_out) {
    if (!ctx || !ctx->sim) return STATUS_ERR_NULL;

    const DemandPredictor *pred = ctx->active_pred;
    const char *eff_name = ctx->active_pred_name ? ctx->active_pred_name : "oracle";

    if (pred_name != NULL) {
        if (strcmp(pred_name, "oracle") == 0) {
            pred = NULL;
            eff_name = "oracle";
        } else if (strcmp(pred_name, "ma") == 0) {
            if (!ctx->pred_ma) return STATUS_ERR_INVALID;
            pred = ctx->pred_ma;
            eff_name = "ma";
        } else if (strcmp(pred_name, "rf") == 0) {
            if (!ctx->pred_rf) return STATUS_ERR_INVALID;
            pred = ctx->pred_rf;
            eff_name = "rf";
        } else {
            return STATUS_ERR_INVALID;
        }
    }

    Status st = sim_set_policy(ctx->sim, policy, pred);
    if (st != STATUS_OK) return st;

    ctx->sim_cfg.policy = policy;
    ctx->active_pred = pred;
    ctx->active_pred_name = eff_name;

    size_t moves = 0;
    if (force) {
        st = sim_force_decision(ctx->sim, &moves);
        if (st != STATUS_OK) return st;
    }
    if (moves_applied_out) *moves_applied_out = moves;

    app_sync_after_step(ctx);
    ctx->generation++;
    return STATUS_OK;
}

Status app_manual_move(AppCtx *ctx, int bus_id, int to_route) {
    if (!ctx || !ctx->sim) return STATUS_ERR_NULL;
    Status st = sim_request_move(ctx->sim, bus_id, to_route);
    if (st != STATUS_OK) return st;
    app_sync_after_step(ctx);
    ctx->generation++;
    return STATUS_OK;
}

Status app_inject(AppCtx *ctx, size_t stop_idx, int count, int ride_stops, double *time_s_out) {
    if (!ctx || !ctx->sim) return STATUS_ERR_NULL;
    if (count < 1 || count > 500 || ride_stops < 0) return STATUS_ERR_INVALID;
    double time_s = sim_now_min(ctx->sim) * 60.0;
    Status st = sim_inject_arrivals(ctx->sim, stop_idx, time_s, count, ride_stops);
    if (st != STATUS_OK) return st;
    /* Flush stop at current timestamp so queue length updates immediately */
    (void)sim_queue_length(ctx->sim, stop_idx);
    app_sync_after_step(ctx);
    ctx->generation++;
    if (time_s_out) *time_s_out = time_s;
    return STATUS_OK;
}

Status app_undo(AppCtx *ctx, char *desc_buf, size_t desc_cap, size_t *history_size_out) {
    if (!ctx || !ctx->net) return STATUS_ERR_NULL;
    RouteManager *rm = network_route_manager(ctx->net);
    if (!rm) return STATUS_ERR_NULL;

    Status st = rm_undo(rm, desc_buf, desc_cap);
    if (st != STATUS_OK) return st;

    ctx->last_rm_history_size = rm_history_size(rm);
    app_bump_geometry_version(ctx);
    if (ctx->registry && ctx->sim) {
        registry_sync(ctx->registry, ctx->sim);
    }
    if (history_size_out) *history_size_out = ctx->last_rm_history_size;
    return STATUS_OK;
}

Status app_set_last_optimize(AppCtx *ctx, const char *json_str) {
    if (!ctx || !json_str) return STATUS_ERR_NULL;
    char *copy = dup_str(json_str);
    if (!copy) return STATUS_ERR_NOMEM;
    free(ctx->last_optimize_json);
    ctx->last_optimize_json = copy;
    return STATUS_OK;
}

Status app_set_eval_path_for_test(AppCtx *ctx, const char *path) {
    if (!ctx || !path) return STATUS_ERR_NULL;
    char *copy = dup_str(path);
    if (!copy) return STATUS_ERR_NOMEM;
    free(ctx->eval_path_copy);
    ctx->eval_path_copy = copy;
    return STATUS_OK;
}

void app_bump_geometry_version(AppCtx *ctx) {
    if (!ctx) return;
    ctx->geometry_version++;
    ctx->generation++;
}

const AppOptions *app_options(const AppCtx *ctx) { return ctx ? &ctx->opt : NULL; }
Graph *app_graph_mut(AppCtx *ctx) { return ctx ? ctx->graph : NULL; }
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
const char *app_last_optimize_json(const AppCtx *ctx) { return ctx ? ctx->last_optimize_json : NULL; }
const char *app_eval_path(const AppCtx *ctx) { return ctx ? ctx->eval_path_copy : NULL; }
const char *app_traffic_source_str(const AppCtx *ctx) {
    if (!ctx) return "simulated";
    return (ctx->traffic_source == TRAFFIC_LIVE) ? "live" : "simulated";
}
int app_traffic_calls(const AppCtx *ctx) { return ctx ? ctx->traffic_calls : 0; }
size_t app_traffic_live_corridors(const AppCtx *ctx) { return ctx ? ctx->traffic_live_corridors : 0U; }
const char *app_traffic_last_error(const AppCtx *ctx) {
    return ctx ? ctx->traffic_last_error : "";
}
int app_is_running(const AppCtx *ctx) { return ctx ? ctx->running : 0; }
double app_speed(const AppCtx *ctx) { return ctx ? ctx->speed : 0.0; }
uint64_t app_geometry_version(const AppCtx *ctx) { return ctx ? ctx->geometry_version : 0ULL; }
uint64_t app_generation(const AppCtx *ctx) { return ctx ? ctx->generation : 0ULL; }
const char *app_cors_origin(const AppCtx *ctx) {
    return (ctx && ctx->opt.cors_origin) ? ctx->opt.cors_origin : "http://localhost:5173";
}
