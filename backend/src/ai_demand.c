#include "ai_demand.h"
#include "hashtable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct StatRecord {
    double sum;
    size_t count;
} StatRecord;

typedef struct MovingAvgModel {
    HashTable *table_stop; /* key: stop_node*1000 + hour*10 + day_type */
    HashTable *table_type; /* key: stop_type*1000 + hour*10 + day_type */
    double hour_sum[24];
    size_t hour_cnt[24];
    double global_sum;
    size_t global_cnt;
} MovingAvgModel;

typedef struct OlsBandModel {
    double b[4][10]; /* 4 bands, 10 coefficients */
} OlsBandModel;

struct DemandPredictor {
    DemandModelKind kind;
    union {
        MovingAvgModel ma;
        OlsBandModel ols;
        ForestModel *fm;
    } u;
};

static inline int get_hour_band(int hour) {
    if (hour >= 6 && hour <= 10) return 1;  /* morning */
    if (hour >= 11 && hour <= 15) return 2; /* midday */
    if (hour >= 16 && hour <= 21) return 3; /* evening */
    return 0;                               /* night: 22,23,0..5 */
}

DemandPredictor *dp_train_moving_avg(const DemandRow *rows, size_t n) {
    if (!rows || n == 0) {
        return NULL;
    }

    DemandPredictor *p = (DemandPredictor *)calloc(1, sizeof(DemandPredictor));
    if (!p) return NULL;
    p->kind = DEMAND_MODEL_MOVING_AVG;

    p->u.ma.table_stop = ht_create(HT_KEY_INT, free);
    p->u.ma.table_type = ht_create(HT_KEY_INT, free);
    if (!p->u.ma.table_stop || !p->u.ma.table_type) {
        dp_destroy(p);
        return NULL;
    }

    for (size_t i = 0; i < n; i++) {
        const DemandRow *r = &rows[i];
        if (r->hour < 0 || r->hour > 23) continue;

        /* Stop-level key */
        int64_t key_stop = (int64_t)r->stop_node * 1000 + r->hour * 10 + (int)r->day_type;
        StatRecord *rec = (StatRecord *)ht_get_int(p->u.ma.table_stop, key_stop);
        if (!rec) {
            rec = (StatRecord *)calloc(1, sizeof(StatRecord));
            if (!rec) { dp_destroy(p); return NULL; }
            ht_put_int(p->u.ma.table_stop, key_stop, rec);
        }
        rec->sum += r->passengers;
        rec->count++;

        /* Type-level key */
        int64_t key_type = (int64_t)r->stop_type * 1000 + r->hour * 10 + (int)r->day_type;
        StatRecord *rec_t = (StatRecord *)ht_get_int(p->u.ma.table_type, key_type);
        if (!rec_t) {
            rec_t = (StatRecord *)calloc(1, sizeof(StatRecord));
            if (!rec_t) { dp_destroy(p); return NULL; }
            ht_put_int(p->u.ma.table_type, key_type, rec_t);
        }
        rec_t->sum += r->passengers;
        rec_t->count++;

        /* Hour level */
        p->u.ma.hour_sum[r->hour] += r->passengers;
        p->u.ma.hour_cnt[r->hour]++;

        p->u.ma.global_sum += r->passengers;
        p->u.ma.global_cnt++;
    }

    return p;
}

static int solve_linear_system(double A[10][10], const double v[10], double b[10]) {
    double M[10][11];
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            M[i][j] = A[i][j];
        }
        M[i][10] = v[i];
    }

    for (int col = 0; col < 10; col++) {
        int max_r = col;
        double max_val = fabs(M[col][col]);
        for (int r = col + 1; r < 10; r++) {
            double abs_val = fabs(M[r][col]);
            if (abs_val > max_val) {
                max_val = abs_val;
                max_r = r;
            }
        }
        if (max_val < 1e-12) {
            return 0; /* Singular or degenerate */
        }
        if (max_r != col) {
            for (int j = col; j <= 10; j++) {
                double tmp = M[col][j];
                M[col][j] = M[max_r][j];
                M[max_r][j] = tmp;
            }
        }

        double pivot = M[col][col];
        for (int r = col + 1; r < 10; r++) {
            double factor = M[r][col] / pivot;
            for (int j = col; j <= 10; j++) {
                M[r][j] -= factor * M[col][j];
            }
        }
    }

    /* Back-substitution */
    for (int i = 9; i >= 0; i--) {
        double rhs = M[i][10];
        for (int j = i + 1; j < 10; j++) {
            rhs -= M[i][j] * b[j];
        }
        b[i] = rhs / M[i][i];
    }
    return 1;
}

DemandPredictor *dp_train_ols_band(const DemandRow *rows, size_t n) {
    if (!rows || n == 0) {
        return NULL;
    }

    DemandPredictor *p = (DemandPredictor *)calloc(1, sizeof(DemandPredictor));
    if (!p) return NULL;
    p->kind = DEMAND_MODEL_OLS_BAND;

    double A[4][10][10];
    double v[4][10];
    size_t band_counts[4];
    double band_sum_y[4];

    memset(A, 0, sizeof(A));
    memset(v, 0, sizeof(v));
    memset(band_counts, 0, sizeof(band_counts));
    memset(band_sum_y, 0, sizeof(band_sum_y));

    for (size_t i = 0; i < n; i++) {
        const DemandRow *r = &rows[i];
        if (r->hour < 0 || r->hour > 23) continue;

        int band = get_hour_band(r->hour);
        band_counts[band]++;
        band_sum_y[band] += r->passengers;

        /* Features:
         * 0: intercept (1.0)
         * 1: hour
         * 2: weekend
         * 3: holiday
         * 4: rain
         * 5: event
         * 6: type_hub
         * 7: type_office
         * 8: type_market
         * 9: stop_scale
         * (residential is reference level)
         */
        double x[10];
        x[0] = 1.0;
        x[1] = (double)r->hour;
        x[2] = (r->day_type == DAY_WEEKEND) ? 1.0 : 0.0;
        x[3] = (r->day_type == DAY_HOLIDAY) ? 1.0 : 0.0;
        x[4] = (r->rain != 0) ? 1.0 : 0.0;
        x[5] = (r->event != 0) ? 1.0 : 0.0;
        x[6] = (r->stop_type == STOP_HUB) ? 1.0 : 0.0;
        x[7] = (r->stop_type == STOP_OFFICE) ? 1.0 : 0.0;
        x[8] = (r->stop_type == STOP_MARKET) ? 1.0 : 0.0;
        x[9] = r->stop_scale;

        for (int r_idx = 0; r_idx < 10; r_idx++) {
            v[band][r_idx] += x[r_idx] * r->passengers;
            for (int c_idx = 0; c_idx < 10; c_idx++) {
                A[band][r_idx][c_idx] += x[r_idx] * x[c_idx];
            }
        }
    }

    for (int band = 0; band < 4; band++) {
        if (band_counts[band] < 10) {
            /* Fallback to row mean */
            p->u.ols.b[band][0] = (band_counts[band] > 0) ? (band_sum_y[band] / (double)band_counts[band]) : 0.0;
            for (int j = 1; j < 10; j++) {
                p->u.ols.b[band][j] = 0.0;
            }
        } else {
            /* Add ridge = 1e-6 to diagonal entries of all columns except intercept */
            for (int j = 1; j < 10; j++) {
                A[band][j][j] += 1e-6;
            }
            double coefs[10];
            memset(coefs, 0, sizeof(coefs));
            int ok = solve_linear_system(A[band], v[band], coefs);
            if (ok) {
                for (int j = 0; j < 10; j++) {
                    p->u.ols.b[band][j] = coefs[j];
                }
            } else {
                p->u.ols.b[band][0] = band_sum_y[band] / (double)band_counts[band];
                for (int j = 1; j < 10; j++) {
                    p->u.ols.b[band][j] = 0.0;
                }
            }
        }
    }

    return p;
}

DemandPredictor *dp_from_forest(ForestModel *fm) {
    if (!fm) return NULL;
    DemandPredictor *p = (DemandPredictor *)calloc(1, sizeof(DemandPredictor));
    if (!p) return NULL;
    p->kind = DEMAND_MODEL_FOREST;
    p->u.fm = fm;
    return p;
}

void dp_destroy(DemandPredictor *p) {
    if (!p) return;
    if (p->kind == DEMAND_MODEL_MOVING_AVG) {
        if (p->u.ma.table_stop) ht_destroy(p->u.ma.table_stop);
        if (p->u.ma.table_type) ht_destroy(p->u.ma.table_type);
    } else if (p->kind == DEMAND_MODEL_FOREST) {
        if (p->u.fm) forest_destroy(p->u.fm);
    }
    free(p);
}

double dp_predict(const DemandPredictor *p, int stop_node, StopType type, double scale, int hour, DayType day_type, int rain, int event) {
    if (!p || hour < 0 || hour > 23 || scale <= 0.0) {
        return -1.0;
    }
    if ((int)type < 0 || (int)type > 3) {
        return -1.0;
    }
    if ((int)day_type < 0 || (int)day_type > 2) {
        return -1.0;
    }

    double pred = 0.0;

    switch (p->kind) {
        case DEMAND_MODEL_MOVING_AVG: {
            int64_t key_stop = (int64_t)stop_node * 1000 + hour * 10 + (int)day_type;
            const StatRecord *rec = (const StatRecord *)ht_get_int(p->u.ma.table_stop, key_stop);
            if (rec && rec->count > 0) {
                pred = rec->sum / (double)rec->count;
            } else {
                int64_t key_type = (int64_t)type * 1000 + hour * 10 + (int)day_type;
                const StatRecord *rec_t = (const StatRecord *)ht_get_int(p->u.ma.table_type, key_type);
                if (rec_t && rec_t->count > 0) {
                    pred = rec_t->sum / (double)rec_t->count;
                } else if (p->u.ma.hour_cnt[hour] > 0) {
                    pred = p->u.ma.hour_sum[hour] / (double)p->u.ma.hour_cnt[hour];
                } else {
                    pred = 0.0;
                }
            }
            break;
        }

        case DEMAND_MODEL_OLS_BAND: {
            int band = get_hour_band(hour);
            const double *b = p->u.ols.b[band];
            double x[10];
            x[0] = 1.0;
            x[1] = (double)hour;
            x[2] = (day_type == DAY_WEEKEND) ? 1.0 : 0.0;
            x[3] = (day_type == DAY_HOLIDAY) ? 1.0 : 0.0;
            x[4] = (rain != 0) ? 1.0 : 0.0;
            x[5] = (event != 0) ? 1.0 : 0.0;
            x[6] = (type == STOP_HUB) ? 1.0 : 0.0;
            x[7] = (type == STOP_OFFICE) ? 1.0 : 0.0;
            x[8] = (type == STOP_MARKET) ? 1.0 : 0.0;
            x[9] = scale;

            pred = 0.0;
            for (int j = 0; j < 10; j++) {
                pred += b[j] * x[j];
            }
            break;
        }

        case DEMAND_MODEL_FOREST: {
            double feats[DEMAND_NUM_FEATURES];
            demand_features(type, scale, hour, day_type, rain, event, feats);
            pred = forest_predict(p->u.fm, feats);
            if (isnan(pred)) {
                return -1.0;
            }
            break;
        }

        default:
            return -1.0;
    }

    if (pred < 0.0) {
        pred = 0.0;
    }
    return pred;
}

DemandModelKind dp_kind(const DemandPredictor *p) {
    return p ? p->kind : DEMAND_MODEL_MOVING_AVG;
}

const char *dp_name(const DemandPredictor *p) {
    if (!p) return "unknown";
    switch (p->kind) {
        case DEMAND_MODEL_MOVING_AVG: return "moving_average";
        case DEMAND_MODEL_OLS_BAND:   return "ols_per_band";
        case DEMAND_MODEL_FOREST:     return "random_forest";
        default:                      return "unknown";
    }
}

Status dp_evaluate(const DemandPredictor *p, const DemandRow *rows, size_t n, DemandEval *overall, DemandEval by_category[DEMAND_NUM_CATEGORIES]) {
    if (!p || !rows || !overall) {
        return STATUS_ERR_NULL;
    }

    memset(overall, 0, sizeof(DemandEval));
    if (by_category) {
        memset(by_category, 0, DEMAND_NUM_CATEGORIES * sizeof(DemandEval));
    }

    if (n == 0) {
        return STATUS_OK;
    }

    double tot_abs_err = 0.0;
    double tot_sq_err = 0.0;
    double tot_abs_lam = 0.0;
    double tot_pass = 0.0;

    double cat_abs_err[DEMAND_NUM_CATEGORIES];
    double cat_sq_err[DEMAND_NUM_CATEGORIES];
    double cat_abs_lam[DEMAND_NUM_CATEGORIES];
    double cat_pass[DEMAND_NUM_CATEGORIES];
    size_t cat_n[DEMAND_NUM_CATEGORIES];

    memset(cat_abs_err, 0, sizeof(cat_abs_err));
    memset(cat_sq_err, 0, sizeof(cat_sq_err));
    memset(cat_abs_lam, 0, sizeof(cat_abs_lam));
    memset(cat_pass, 0, sizeof(cat_pass));
    memset(cat_n, 0, sizeof(cat_n));

    for (size_t i = 0; i < n; i++) {
        const DemandRow *r = &rows[i];
        double y_hat = dp_predict(p, r->stop_node, r->stop_type, r->stop_scale, r->hour, r->day_type, r->rain, r->event);
        if (y_hat < 0.0) y_hat = 0.0;

        double err_noisy = fabs(y_hat - r->passengers);
        double sq_err = (y_hat - r->passengers) * (y_hat - r->passengers);
        double err_lam = fabs(y_hat - r->lambda);

        tot_abs_err += err_noisy;
        tot_sq_err += sq_err;
        tot_abs_lam += err_lam;
        tot_pass += r->passengers;

        DemandCategory cat = demand_row_category(r);
        if ((int)cat >= 0 && (int)cat < DEMAND_NUM_CATEGORIES) {
            cat_abs_err[cat] += err_noisy;
            cat_sq_err[cat] += sq_err;
            cat_abs_lam[cat] += err_lam;
            cat_pass[cat] += r->passengers;
            cat_n[cat]++;
        }
    }

    overall->n = n;
    overall->mae_noisy = tot_abs_err / (double)n;
    overall->rmse_noisy = sqrt(tot_sq_err / (double)n);
    overall->wape_noisy = (tot_pass > 0.0) ? (tot_abs_err / tot_pass) : 0.0;
    overall->mae_lambda = tot_abs_lam / (double)n;

    if (by_category) {
        for (int c = 0; c < DEMAND_NUM_CATEGORIES; c++) {
            by_category[c].n = cat_n[c];
            if (cat_n[c] > 0) {
                by_category[c].mae_noisy = cat_abs_err[c] / (double)cat_n[c];
                by_category[c].rmse_noisy = sqrt(cat_sq_err[c] / (double)cat_n[c]);
                by_category[c].wape_noisy = (cat_pass[c] > 0.0) ? (cat_abs_err[c] / cat_pass[c]) : 0.0;
                by_category[c].mae_lambda = cat_abs_lam[c] / (double)cat_n[c];
            } else {
                by_category[c].mae_noisy = 0.0;
                by_category[c].rmse_noisy = 0.0;
                by_category[c].wape_noisy = 0.0;
                by_category[c].mae_lambda = 0.0;
            }
        }
    }

    return STATUS_OK;
}
