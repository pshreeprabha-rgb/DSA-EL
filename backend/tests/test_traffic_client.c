#define _POSIX_C_SOURCE 200809L

#include "test_util.h"
#include "traffic_client.h"
#include "graph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#define ASSERT_TRUE(cond) TEST_ASSERT(cond)
#define ASSERT_EQ(a,b)    TEST_ASSERT_EQ_INT(a,b)
#define ASSERT_NULL(p)    TEST_ASSERT_NULL(p)
#define ASSERT_NOT_NULL(p) TEST_ASSERT_NOT_NULL(p)
#define ASSERT_DOUBLE_EQ(a,b,tol) TEST_ASSERT(fabs((a)-(b))<=(tol))
#define ASSERT_STR_EQ(a,b) TEST_ASSERT(strcmp((a),(b))==0)

static const char *FAKE_KEY = "TEST_KEY_123";

/* ---- Mock HTTP fetch ---- */
typedef struct {
    const char *response_body;
    long http_status;
    int call_count;
    int fail;
} MockCtx;

static Status mock_fetch(const char *url, char **body_out, long *http_status, void *ctx) {
    (void)url;
    MockCtx *m = (MockCtx *)ctx;
    m->call_count++;
    *http_status = m->http_status;
    if (m->fail || !m->response_body) return STATUS_ERR_INVALID;
    *body_out = strdup(m->response_body);
    if (!*body_out) return STATUS_ERR_NOMEM;
    return STATUS_OK;
}

/* ---- URL builder tests ---- */
static void test_build_url(void) {
    char buf[512];
    Status st = tomtom_build_url(buf, sizeof(buf), FAKE_KEY, 12.9716, 77.5946);
    ASSERT_EQ(st, STATUS_OK);
    /* Must contain key= and the fake key */
    ASSERT_TRUE(strstr(buf, "key=") != NULL);
    ASSERT_TRUE(strstr(buf, FAKE_KEY) != NULL);
    /* Verify coordinates appear */
    ASSERT_TRUE(strstr(buf, "12.971600") != NULL || strstr(buf, "12.9716") != NULL);

    /* Truncation: too small buffer */
    char tiny[10];
    st = tomtom_build_url(tiny, sizeof(tiny), FAKE_KEY, 12.0, 77.0);
    ASSERT_EQ(st, STATUS_ERR_INVALID);

    /* Empty key */
    st = tomtom_build_url(buf, sizeof(buf), "", 12.0, 77.0);
    ASSERT_EQ(st, STATUS_ERR_INVALID);
}

static void test_redact_url(void) {
    char url[512];
    tomtom_build_url(url, sizeof(url), FAKE_KEY, 12.0, 77.0);

    char redacted[512];
    tomtom_redact_url(url, redacted, sizeof(redacted));

    /* Key must NOT appear */
    ASSERT_TRUE(strstr(redacted, FAKE_KEY) == NULL);
    /* *** must appear */
    ASSERT_TRUE(strstr(redacted, "***") != NULL);
}

/* ---- JSON parse tests ---- */

static const char *GOOD_JSON =
    "{\"flowSegmentData\":{\"currentSpeed\":20,\"freeFlowSpeed\":40,\"confidence\":0.9}}";

static const char *CLOSURE_JSON =
    "{\"flowSegmentData\":{\"currentSpeed\":0,\"freeFlowSpeed\":50,\"confidence\":1.0,\"roadClosure\":true}}";

static const char *FAST_JSON =
    "{\"flowSegmentData\":{\"currentSpeed\":60,\"freeFlowSpeed\":40,\"confidence\":1.0}}";

static const char *ERROR_JSON =
    "{\"error\":{\"code\":403,\"message\":\"Forbidden\"}}";

static void test_parse_flow_json(void) {
    TomTomFlow f;

    /* Good response: factor = 40/20 = 2.0 */
    Status st = tomtom_parse_flow_json(GOOD_JSON, &f);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_DOUBLE_EQ(f.current_speed, 20.0, 1e-9);
    ASSERT_DOUBLE_EQ(f.free_flow_speed, 40.0, 1e-9);
    ASSERT_DOUBLE_EQ(f.confidence, 0.9, 1e-9);
    ASSERT_EQ(f.road_closure, 0);
    ASSERT_DOUBLE_EQ(tomtom_traffic_factor(&f), 2.0, 1e-9);

    /* Road closure -> max factor */
    st = tomtom_parse_flow_json(CLOSURE_JSON, &f);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(f.road_closure, 1);
    ASSERT_DOUBLE_EQ(tomtom_traffic_factor(&f), GRAPH_MAX_TRAFFIC_FACTOR, 1e-9);

    /* Current faster than free flow -> clamped 1.0 */
    st = tomtom_parse_flow_json(FAST_JSON, &f);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_DOUBLE_EQ(tomtom_traffic_factor(&f), 1.0, 1e-9);

    /* currentSpeed = 0 -> max factor */
    const char *zero_speed_json =
        "{\"flowSegmentData\":{\"currentSpeed\":0,\"freeFlowSpeed\":50,\"confidence\":1.0}}";
    st = tomtom_parse_flow_json(zero_speed_json, &f);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_DOUBLE_EQ(tomtom_traffic_factor(&f), GRAPH_MAX_TRAFFIC_FACTOR, 1e-9);

    /* Error JSON -> STATUS_ERR_INVALID */
    st = tomtom_parse_flow_json(ERROR_JSON, &f);
    ASSERT_EQ(st, STATUS_ERR_INVALID);

    /* Garbage text */
    st = tomtom_parse_flow_json("not json at all", &f);
    ASSERT_EQ(st, STATUS_ERR_INVALID);

    /* Missing flowSegmentData */
    st = tomtom_parse_flow_json("{\"foo\":1}", &f);
    ASSERT_EQ(st, STATUS_ERR_INVALID);

    /* Missing required fields */
    st = tomtom_parse_flow_json("{\"flowSegmentData\":{\"confidence\":0.9}}", &f);
    ASSERT_EQ(st, STATUS_ERR_INVALID);

    /* Missing optional confidence -> defaults to 1.0 */
    st = tomtom_parse_flow_json(
        "{\"flowSegmentData\":{\"currentSpeed\":30,\"freeFlowSpeed\":60}}", &f);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_DOUBLE_EQ(f.confidence, 1.0, 1e-9);
}

/* ---- Updater tests ---- */

static Graph *small_graph(void) {
    Graph *g = graph_create();
    graph_add_node(g, 1, 12.9716, 77.5946, "A", 0);
    graph_add_node(g, 2, 12.9700, 77.5920, "B", 0);
    graph_add_edge(g, 0, 1, 500.0, 50.0, "MG Rd", ROAD_PRIMARY);
    graph_add_edge(g, 1, 0, 500.0, 50.0, "MG Rd", ROAD_PRIMARY);
    return g;
}

static void test_updater_live_success(void) {
    Graph *g = small_graph();

    TrafficCorridor corr = {"Test Corridor", 12.9710, 77.5933, 500.0};
    MockCtx mock = {GOOD_JSON, 200, 0, 0};

    TrafficConfig cfg = {FAKE_KEY, 10, 1, 0, mock_fetch, &mock};
    TrafficUpdater *tu = tu_create(g, &corr, 1, &cfg);
    ASSERT_NOT_NULL(tu);

    Status st = tu_update(tu, 0.0, 9.0 * 60.0, 0);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(tu_source(tu), TRAFFIC_LIVE);
    ASSERT_EQ(tu_call_count(tu), 1);
    ASSERT_EQ(tu_live_corridor_count(tu), 1);

    /* Second update within cache: no new call */
    st = tu_update(tu, 0.5, 9.0 * 60.0, 0);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(tu_call_count(tu), 1);

    /* After cache expires: one more call */
    st = tu_update(tu, 2.0, 9.0 * 60.0, 0);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(tu_call_count(tu), 2);

    tu_destroy(tu);
    graph_destroy(g);
}

static void test_updater_http_403_fallback(void) {
    Graph *g = small_graph();
    TrafficCorridor corr = {"Corridor", 12.9710, 77.5933, 500.0};
    MockCtx mock = {ERROR_JSON, 403, 0, 0};

    TrafficConfig cfg = {FAKE_KEY, 10, 1, 0, mock_fetch, &mock};
    TrafficUpdater *tu = tu_create(g, &corr, 1, &cfg);
    ASSERT_NOT_NULL(tu);

    tu_update(tu, 0.0, 9.0 * 60.0, 0);
    ASSERT_EQ(tu_source(tu), TRAFFIC_SIMULATED);
    ASSERT_TRUE(tu_last_error(tu)[0] != '\0');
    /* last_error must NOT contain the fake key */
    ASSERT_TRUE(strstr(tu_last_error(tu), FAKE_KEY) == NULL);

    tu_destroy(tu);
    graph_destroy(g);
}

static void test_updater_low_confidence_ignored(void) {
    Graph *g = small_graph();
    TrafficCorridor corr = {"Corridor", 12.9710, 77.5933, 500.0};
    const char *low_conf_json =
        "{\"flowSegmentData\":{\"currentSpeed\":20,\"freeFlowSpeed\":40,\"confidence\":0.3}}";
    MockCtx mock = {low_conf_json, 200, 0, 0};

    TrafficConfig cfg = {FAKE_KEY, 10, 1, 0, mock_fetch, &mock};
    TrafficUpdater *tu = tu_create(g, &corr, 1, &cfg);

    tu_update(tu, 0.0, 9.0 * 60.0, 0);
    ASSERT_EQ(tu_source(tu), TRAFFIC_SIMULATED);
    ASSERT_EQ(tu_live_corridor_count(tu), 0);

    tu_destroy(tu);
    graph_destroy(g);
}

static void test_updater_max_calls(void) {
    Graph *g = small_graph();
    TrafficCorridor corr = {"Corridor", 12.9710, 77.5933, 500.0};
    MockCtx mock = {GOOD_JSON, 200, 0, 0};

    TrafficConfig cfg = {FAKE_KEY, 2, 0, 0, mock_fetch, &mock};
    TrafficUpdater *tu = tu_create(g, &corr, 1, &cfg);

    for (int i = 0; i < 5; i++) {
        tu_update(tu, (double)i * 1000.0, 9.0 * 60.0, 0);
    }
    ASSERT_TRUE(tu_call_count(tu) <= 2);

    tu_destroy(tu);
    graph_destroy(g);
}

static void test_updater_offline(void) {
    Graph *g = small_graph();
    TrafficCorridor corr = {"Corridor", 12.9710, 77.5933, 500.0};
    MockCtx mock = {GOOD_JSON, 200, 0, 0};

    TrafficConfig cfg = {FAKE_KEY, 10, 1, 1, mock_fetch, &mock};  /* offline=1 */
    TrafficUpdater *tu = tu_create(g, &corr, 1, &cfg);

    tu_update(tu, 0.0, 9.0 * 60.0, 0);
    ASSERT_EQ(tu_call_count(tu), 0);
    ASSERT_EQ(tu_source(tu), TRAFFIC_SIMULATED);

    tu_destroy(tu);
    graph_destroy(g);
}

static void test_updater_no_key(void) {
    Graph *g = small_graph();
    TrafficCorridor corr = {"Corridor", 12.9710, 77.5933, 500.0};
    MockCtx mock = {GOOD_JSON, 200, 0, 0};

    TrafficConfig cfg = {NULL, 10, 1, 0, mock_fetch, &mock};
    /* Ensure TOMTOM_API_KEY is not set in environment */
    unsetenv("TOMTOM_API_KEY");

    TrafficUpdater *tu = tu_create(g, &corr, 1, &cfg);

    tu_update(tu, 0.0, 9.0 * 60.0, 0);
    ASSERT_EQ(tu_call_count(tu), 0);
    ASSERT_EQ(tu_source(tu), TRAFFIC_SIMULATED);

    tu_destroy(tu);
    graph_destroy(g);
}

static void test_load_corridors(void) {
    const char *path = "test_corridors_tmp.txt";
    FILE *f = fopen(path, "w");
    fprintf(f, "# comment\n");
    fprintf(f, "\n");
    fprintf(f, "MG Road\t12.9756\t77.6066\t250.0\n");
    fprintf(f, "Majestic\t12.9774\t77.5708\t250.0\n");
    fclose(f);

    TrafficCorridor corr[10];
    size_t count = 0;
    Status st = tu_load_corridors(path, corr, 10, &count);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(count, 2);
    ASSERT_STR_EQ(corr[0].name, "MG Road");
    ASSERT_DOUBLE_EQ(corr[0].lat, 12.9756, 1e-9);
    ASSERT_DOUBLE_EQ(corr[0].lng, 77.6066, 1e-9);
    ASSERT_DOUBLE_EQ(corr[0].radius_m, 250.0, 1e-9);
    remove(path);

    /* Missing file */
    st = tu_load_corridors("non_existent_xyz.txt", corr, 10, &count);
    ASSERT_EQ(st, STATUS_ERR_NOT_FOUND);

    /* Bad line (missing tab) */
    f = fopen("bad_corr.txt", "w");
    fprintf(f, "Name NoTabs\n");
    fclose(f);
    st = tu_load_corridors("bad_corr.txt", corr, 10, &count);
    ASSERT_EQ(st, STATUS_ERR_INVALID);
    remove("bad_corr.txt");
}

static void test_env_loader(void) {
    /* Write a temporary .env file with fake key */
    const char *tmp_env = "tmp_test_env_file.env";
    FILE *f = fopen(tmp_env, "w");
    /* Test all features */
    fprintf(f, "# comment\n");
    fprintf(f, "\n");
    fprintf(f, "PLAIN_VAR=hello\n");
    fprintf(f, "QUOTED_DOUBLE=\"double quoted\"\n");
    fprintf(f, "QUOTED_SINGLE='single quoted'\n");
    fprintf(f, "EXPORT_VAR=export_val\n");
    /* CRLF line endings */
    fprintf(f, "CRLF_VAR=no_cr\r\n");
    fclose(f);

    /* Unset test vars first */
    unsetenv("PLAIN_VAR");
    unsetenv("QUOTED_DOUBLE");
    unsetenv("QUOTED_SINGLE");
    unsetenv("EXPORT_VAR");
    unsetenv("CRLF_VAR");

    Status st = env_load_file(tmp_env);
    ASSERT_EQ(st, STATUS_OK);

    const char *v1 = getenv("PLAIN_VAR");
    ASSERT_NOT_NULL(v1);
    ASSERT_STR_EQ(v1, "hello");

    const char *v2 = getenv("QUOTED_DOUBLE");
    ASSERT_NOT_NULL(v2);
    ASSERT_STR_EQ(v2, "double quoted");

    const char *v3 = getenv("QUOTED_SINGLE");
    ASSERT_NOT_NULL(v3);
    ASSERT_STR_EQ(v3, "single quoted");

    /* CRLF value must NOT contain \r */
    const char *v5 = getenv("CRLF_VAR");
    ASSERT_NOT_NULL(v5);
    ASSERT_TRUE(strchr(v5, '\r') == NULL);
    ASSERT_STR_EQ(v5, "no_cr");

    /* setenv with no-overwrite: pre-set variable should not change */
    setenv("NOOVER_VAR", "original", 1);
    FILE *f2 = fopen("tmp_noover.env", "w");
    fprintf(f2, "NOOVER_VAR=changed\n");
    fclose(f2);
    env_load_file("tmp_noover.env");
    ASSERT_STR_EQ(getenv("NOOVER_VAR"), "original");
    remove("tmp_noover.env");

    /* Missing file */
    st = env_load_file("does_not_exist_xyz.env");
    ASSERT_EQ(st, STATUS_ERR_NOT_FOUND);

    /* Invalid name */
    FILE *f3 = fopen("tmp_bad.env", "w");
    fprintf(f3, "123BAD=val\n");
    fclose(f3);
    st = env_load_file("tmp_bad.env");
    ASSERT_EQ(st, STATUS_ERR_INVALID);
    remove("tmp_bad.env");

    /* Over-long line */
    FILE *f4 = fopen("tmp_long.env", "w");
    for (int i = 0; i < 1030; i++) fputc('X', f4);
    fputc('\n', f4);
    fclose(f4);
    st = env_load_file("tmp_long.env");
    ASSERT_EQ(st, STATUS_ERR_INVALID);
    remove("tmp_long.env");

    /* After loading, build_url + redact_url must NOT contain the fake key */
    char url[512];
    tomtom_build_url(url, sizeof(url), FAKE_KEY, 12.0, 77.0);
    char redacted[512];
    tomtom_redact_url(url, redacted, sizeof(redacted));
    ASSERT_TRUE(strstr(redacted, FAKE_KEY) == NULL);

    remove(tmp_env);

    /* env_load_default: create .env in current dir and test */
    char orig_dir[512];
    if (getcwd(orig_dir, sizeof(orig_dir))) {
        const char *tmp_dir = "/tmp/test_env_default_dir";
        mkdir(tmp_dir, 0755);
        chdir(tmp_dir);

        FILE *fenv = fopen(".env", "w");
        fprintf(fenv, "DEFAULT_ENV_VAR=found_it\n");
        fclose(fenv);

        unsetenv("DEFAULT_ENV_VAR");
        Status std = env_load_default();
        ASSERT_EQ(std, STATUS_OK);
        ASSERT_NOT_NULL(getenv("DEFAULT_ENV_VAR"));

        remove(".env");

        /* Test fallback to ../.env */
        mkdir("subdir", 0755);
        FILE *fenv_parent = fopen(".env", "w");
        fprintf(fenv_parent, "PARENT_ENV_VAR=parent_found\n");
        fclose(fenv_parent);

        chdir("subdir");
        unsetenv("PARENT_ENV_VAR");
        Status std_parent = env_load_default();
        ASSERT_EQ(std_parent, STATUS_OK);
        ASSERT_NOT_NULL(getenv("PARENT_ENV_VAR"));

        chdir("..");
        remove("subdir");
        remove(".env");
        chdir(orig_dir);
    }
}

int main(void) {
    RUN_TEST(test_build_url);
    RUN_TEST(test_redact_url);
    RUN_TEST(test_parse_flow_json);
    RUN_TEST(test_updater_live_success);
    RUN_TEST(test_updater_http_403_fallback);
    RUN_TEST(test_updater_low_confidence_ignored);
    RUN_TEST(test_updater_max_calls);
    RUN_TEST(test_updater_offline);
    RUN_TEST(test_updater_no_key);
    RUN_TEST(test_load_corridors);
    RUN_TEST(test_env_loader);
    return test_summary();
}
