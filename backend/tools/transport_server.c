#include "api_app.h"
#include "api_http.h"
#include "api_json.h"
#include "api_platform.h"
#include "traffic_client.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "Options:\n"
        "  --port N               HTTP listen port (default: 8080)\n"
        "  --bind ADDR            Bind IP address (default: 127.0.0.1)\n"
        "  --data-dir PATH        Data directory (default: data or ../data)\n"
        "  --seed S               PRNG seed (default: 42)\n"
        "  --stops N              Number of stops (default: 60)\n"
        "  --routes R             Number of routes (default: 6)\n"
        "  --buses B              Fleet size (default: 36)\n"
        "  --reserve K            Reserve buses (default: 3)\n"
        "  --policy P             equal|proportional|greedy-spec|greedy-marginal (default: equal)\n"
        "  --predictor PR         oracle|ma|rf (default: ma)\n"
        "  --speed X              Sim seconds per wall second, 1..3600 (default: 60)\n"
        "  --paused               Start simulation paused\n"
        "  --offline              Disable live TomTom API calls\n"
        "  --no-poll              Disable live traffic polling\n"
        "  --cors ORIGIN          Access-Control-Allow-Origin (default: http://localhost:5173)\n",
        prog);
}

static int parse_policy(const char *s, SimPolicy *out) {
    if (strcmp(s, "equal") == 0 || strcmp(s, "fixed-equal") == 0 || strcmp(s, "fixed_equal") == 0) {
        *out = SIM_POLICY_FIXED_EQUAL;
        return 1;
    }
    if (strcmp(s, "proportional") == 0 || strcmp(s, "fixed-prop") == 0 ||
        strcmp(s, "fixed-proportional") == 0 || strcmp(s, "fixed_proportional") == 0) {
        *out = SIM_POLICY_FIXED_PROPORTIONAL;
        return 1;
    }
    if (strcmp(s, "greedy-spec") == 0 || strcmp(s, "greedy_spec") == 0) {
        *out = SIM_POLICY_GREEDY_SPEC;
        return 1;
    }
    if (strcmp(s, "greedy-marginal") == 0 || strcmp(s, "greedy_marginal") == 0) {
        *out = SIM_POLICY_GREEDY_MARGINAL;
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    env_load_default();

    AppOptions opt;
    app_options_default(&opt);

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(arg, "--no-matrix") == 0) {
            fprintf(stderr, "Error: --no-matrix is not allowed for the server\n");
            return 1;
        } else if (strcmp(arg, "--paused") == 0) {
            opt.paused = 1;
        } else if (strcmp(arg, "--offline") == 0) {
            opt.offline = 1;
        } else if (strcmp(arg, "--no-poll") == 0) {
            opt.no_poll = 1;
        } else if (strcmp(arg, "--port") == 0 && i + 1 < argc) {
            if (api_parse_int_strict(argv[++i], 1, 65535, &opt.port) != STATUS_OK) {
                fprintf(stderr, "Error: invalid --port\n");
                return 1;
            }
        } else if (strcmp(arg, "--bind") == 0 && i + 1 < argc) {
            opt.bind_addr = argv[++i];
        } else if (strcmp(arg, "--data-dir") == 0 && i + 1 < argc) {
            opt.data_dir = argv[++i];
        } else if (strcmp(arg, "--seed") == 0 && i + 1 < argc) {
            const char *sv = argv[++i];
            char *end = NULL;
            unsigned long long u = strtoull(sv, &end, 10);
            if (!sv[0] || !end || *end != '\0') {
                fprintf(stderr, "Error: invalid --seed\n");
                return 1;
            }
            opt.seed = (uint64_t)u;
        } else if (strcmp(arg, "--stops") == 0 && i + 1 < argc) {
            int n = 0;
            if (api_parse_int_strict(argv[++i], 2, 10000, &n) != STATUS_OK) {
                fprintf(stderr, "Error: invalid --stops\n");
                return 1;
            }
            opt.n_stops = (size_t)n;
        } else if (strcmp(arg, "--routes") == 0 && i + 1 < argc) {
            int n = 0;
            if (api_parse_int_strict(argv[++i], 1, 1000, &n) != STATUS_OK) {
                fprintf(stderr, "Error: invalid --routes\n");
                return 1;
            }
            opt.n_routes = (size_t)n;
        } else if (strcmp(arg, "--buses") == 0 && i + 1 < argc) {
            int n = 0;
            if (api_parse_int_strict(argv[++i], 1, 10000, &n) != STATUS_OK) {
                fprintf(stderr, "Error: invalid --buses\n");
                return 1;
            }
            opt.n_buses = (size_t)n;
        } else if (strcmp(arg, "--reserve") == 0 && i + 1 < argc) {
            int n = 0;
            if (api_parse_int_strict(argv[++i], 0, 10000, &n) != STATUS_OK) {
                fprintf(stderr, "Error: invalid --reserve\n");
                return 1;
            }
            opt.n_reserve = (size_t)n;
        } else if (strcmp(arg, "--policy") == 0 && i + 1 < argc) {
            if (!parse_policy(argv[++i], &opt.policy)) {
                fprintf(stderr, "Error: invalid --policy\n");
                return 1;
            }
        } else if (strcmp(arg, "--predictor") == 0 && i + 1 < argc) {
            opt.predictor = argv[++i];
            if (strcmp(opt.predictor, "oracle") != 0 &&
                strcmp(opt.predictor, "ma") != 0 &&
                strcmp(opt.predictor, "rf") != 0) {
                fprintf(stderr, "Error: invalid --predictor\n");
                return 1;
            }
        } else if (strcmp(arg, "--speed") == 0 && i + 1 < argc) {
            if (api_parse_double_strict(argv[++i], 1.0, 3600.0, &opt.speed) != STATUS_OK) {
                fprintf(stderr, "Error: invalid --speed (must be 1..3600)\n");
                return 1;
            }
        } else if (strcmp(arg, "--cors") == 0 && i + 1 < argc) {
            opt.cors_origin = argv[++i];
        } else {
            fprintf(stderr, "Error: unknown or incomplete option '%s'\n", arg);
            print_usage(argv[0]);
            return 1;
        }
    }

    AppCtx *app = NULL;
    Status st = app_create(&opt, &app);
    if (st != STATUS_OK || !app) {
        fprintf(stderr, "Failed to initialize server context (%s)\n", status_to_string(st));
        return 1;
    }

    const char *init_pred = app_active_predictor_name(app);

    ApiHttpServer *http = NULL;
    st = api_http_start(app, &http);
    if (st != STATUS_OK || !http) {
        fprintf(stderr, "Failed to start HTTP server on %s:%d (%s)\n",
                opt.bind_addr, opt.port, status_to_string(st));
        app_destroy(app);
        return 1;
    }

    /* Install signal handlers after mg_start() per CivetWeb docs */
    api_signals_install();

    printf("transport_server listening on http://%s:%d (paused=%d, speed=%.1f, predictor=%s)\n",
           opt.bind_addr, opt.port, opt.paused, opt.speed, init_pred);
    fflush(stdout);

    st = app_start_thread(app);
    if (st != STATUS_OK) {
        fprintf(stderr, "Failed to start simulation thread (%s)\n", status_to_string(st));
        api_http_stop(http);
        app_destroy(app);
        return 1;
    }

    while (!api_signal_stop_requested()) {
        api_sleep_ms(100);
    }

    printf("\nShutting down cleanly...\n");
    fflush(stdout);
    app_stop_thread(app);
    api_http_stop(http);
    app_destroy(app);
    return 0;
}

