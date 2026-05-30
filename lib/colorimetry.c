#include "colorimetry.h"
#include "csv.h"
#include "ref_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CMF_WL_START 380
#define CMF_WL_END 780
#define CMF_WL_COUNT (CMF_WL_END - CMF_WL_START + 1)

typedef struct {
    double xbar[CMF_WL_COUNT];
    double ybar[CMF_WL_COUNT];
    double zbar[CMF_WL_COUNT];
    double r[CRI_SAMPLE_COUNT][CMF_WL_COUNT];
    double s0[CMF_WL_COUNT];
    double s1[CMF_WL_COUNT];
    double s2[CMF_WL_COUNT];
    int loaded;
} RefData;

static RefData g_ref;
static int g_use_external_refs = 0;

static int parse_csv_numbers(char *line, double *out, int max) {
    int count = 0;
    char *p = line;
    while (*p && count < max) {
        char *end = NULL;
        double v = strtod(p, &end);
        if (end == p) {
            while (*p && *p != '-' && *p != '+' &&
                   (*p < '0' || *p > '9') && *p != '.') {
                p++;
            }
            if (!*p) break;
            continue;
        }
        out[count++] = v;
        p = end;
    }
    return count;
}

static int read_line_buffer(const unsigned char *data, size_t len,
                            size_t *offset, char *line, size_t cap) {
    if (!data || !offset || !line || cap == 0) return 0;
    if (*offset >= len) return 0;

    size_t i = *offset;
    size_t start = i;
    while (i < len && data[i] != '\n') i++;
    size_t line_len = i - start;
    if (line_len >= cap) line_len = cap - 1;
    memcpy(line, data + start, line_len);
    line[line_len] = '\0';
    if (line_len > 0 && line[line_len - 1] == '\r') {
        line[line_len - 1] = '\0';
    }
    *offset = (i < len && data[i] == '\n') ? i + 1 : i;
    return 1;
}

static void resample_linear(const double *src_wl, const double *src_val,
                            int src_count, double *dst,
                            int dst_start, int dst_count) {
    if (!dst || dst_count <= 0) return;
    if (!src_wl || !src_val || src_count <= 0) {
        for (int i = 0; i < dst_count; i++) dst[i] = 0.0;
        return;
    }

    int j = 0;
    for (int i = 0; i < dst_count; i++) {
        double wl = (double)(dst_start + i);
        while (j + 1 < src_count && src_wl[j + 1] < wl) j++;
        if (wl <= src_wl[0]) {
            dst[i] = src_val[0];
        } else if (wl >= src_wl[src_count - 1]) {
            dst[i] = src_val[src_count - 1];
        } else {
            double w0 = src_wl[j];
            double w1 = src_wl[j + 1];
            double v0 = src_val[j];
            double v1 = src_val[j + 1];
            double t = (wl - w0) / (w1 - w0);
            dst[i] = v0 + (v1 - v0) * t;
        }
    }
}

static int load_cmf(void) {
    FILE *fp = NULL;
    const unsigned char *data = NULL;
    size_t data_len = 0;
    size_t offset = 0;

    if (g_use_external_refs) {
        fp = fopen("ref/CIE_xyz_1931_2deg.csv", "r");
        if (!fp) return -1;
    } else {
        data = _binary_ref_CIE_xyz_1931_2deg_csv_start;
        data_len = (size_t)(_binary_ref_CIE_xyz_1931_2deg_csv_end
                            - _binary_ref_CIE_xyz_1931_2deg_csv_start);
        if (data_len == 0) return -1;
    }

    char line[256];
    int filled = 0;
    for (;;) {
        if (g_use_external_refs) {
            if (!fgets(line, sizeof(line), fp)) break;
        } else {
            if (!read_line_buffer(data, data_len, &offset,
                                  line, sizeof(line))) break;
        }
        int wl = 0;
        double x = 0.0, y = 0.0, z = 0.0;
        if (sscanf(line, "%d,%lf,%lf,%lf", &wl, &x, &y, &z) == 4) {
            if (wl >= CMF_WL_START && wl <= CMF_WL_END) {
                int idx = wl - CMF_WL_START;
                g_ref.xbar[idx] = x;
                g_ref.ybar[idx] = y;
                g_ref.zbar[idx] = z;
                filled++;
            }
        }
    }

    if (fp) fclose(fp);
    return filled > 0 ? 0 : -1;
}

static int load_cri_reflectance(void) {
    FILE *fp = NULL;
    const unsigned char *data = NULL;
    size_t data_len = 0;
    size_t offset = 0;

    if (g_use_external_refs) {
        fp = fopen("ref/CIE_srf_cri.csv", "r");
        if (!fp) return -1;
    } else {
        data = _binary_ref_CIE_srf_cri_csv_start;
        data_len = (size_t)(_binary_ref_CIE_srf_cri_csv_end
                            - _binary_ref_CIE_srf_cri_csv_start);
        if (data_len == 0) return -1;
    }

    double *raw_wl = NULL;
    double *raw_r[CRI_SAMPLE_COUNT] = {0};
    int cap = 0;
    int count = 0;

    char line[512];
    int alloc_failed = 0;
    while (1) {
        if (g_use_external_refs) {
            if (!fgets(line, sizeof(line), fp)) break;
        } else {
            if (!read_line_buffer(data, data_len, &offset,
                                  line, sizeof(line))) break;
        }
        double nums[CRI_SAMPLE_COUNT + 1];
        int n = parse_csv_numbers(line, nums, CRI_SAMPLE_COUNT + 1);
        if (n < CRI_SAMPLE_COUNT + 1) continue;
        if (count >= cap) {
            int new_cap = cap > 0 ? cap * 2 : 128;
            double *new_wl = (double *)realloc(raw_wl,
                                               sizeof(double) * new_cap);
            if (!new_wl) { alloc_failed = 1; break; }
            raw_wl = new_wl;
            for (int i = 0; i < CRI_SAMPLE_COUNT; i++) {
                double *new_r = (double *)realloc(raw_r[i],
                                                  sizeof(double) * new_cap);
                if (!new_r) { alloc_failed = 1; break; }
                raw_r[i] = new_r;
            }
            if (alloc_failed) break;
            cap = new_cap;
        }
        if (count >= cap) break;

        raw_wl[count] = nums[0];
        for (int i = 0; i < CRI_SAMPLE_COUNT; i++) {
            raw_r[i][count] = nums[i + 1];
        }
        count++;
    }

    if (fp) fclose(fp);

    if (alloc_failed || count <= 1) {
        free(raw_wl);
        for (int i = 0; i < CRI_SAMPLE_COUNT; i++) free(raw_r[i]);
        return -1;
    }

    for (int i = 0; i < CRI_SAMPLE_COUNT; i++) {
        resample_linear(raw_wl, raw_r[i], count,
                        g_ref.r[i], CMF_WL_START, CMF_WL_COUNT);
    }

    free(raw_wl);
    for (int i = 0; i < CRI_SAMPLE_COUNT; i++) free(raw_r[i]);
    return 0;
}

static int load_s_data(void) {
    FILE *fp = NULL;
    const unsigned char *data = NULL;
    size_t data_len = 0;
    size_t offset = 0;

    if (g_use_external_refs) {
        fp = fopen("ref/S.csv", "r");
        if (!fp) return -1;
    } else {
        data = _binary_ref_S_csv_start;
        data_len = (size_t)(_binary_ref_S_csv_end
                            - _binary_ref_S_csv_start);
        if (data_len == 0) return -1;
    }

    double *raw_wl = NULL;
    double *raw_s0 = NULL;
    double *raw_s1 = NULL;
    double *raw_s2 = NULL;
    int cap = 0;
    int count = 0;

    char line[256];
    int alloc_failed = 0;
    while (1) {
        if (g_use_external_refs) {
            if (!fgets(line, sizeof(line), fp)) break;
        } else {
            if (!read_line_buffer(data, data_len, &offset,
                                  line, sizeof(line))) break;
        }
        double nums[4];
        int n = parse_csv_numbers(line, nums, 4);
        if (n < 4) continue;
        if (count >= cap) {
            int new_cap = cap > 0 ? cap * 2 : 64;
            double *new_wl = (double *)realloc(raw_wl,
                                               sizeof(double) * new_cap);
            double *new_s0 = (double *)realloc(raw_s0,
                                               sizeof(double) * new_cap);
            double *new_s1 = (double *)realloc(raw_s1,
                                               sizeof(double) * new_cap);
            double *new_s2 = (double *)realloc(raw_s2,
                                               sizeof(double) * new_cap);
            if (!new_wl || !new_s0 || !new_s1 || !new_s2) {
                alloc_failed = 1;
                break;
            }
            raw_wl = new_wl;
            raw_s0 = new_s0;
            raw_s1 = new_s1;
            raw_s2 = new_s2;
            cap = new_cap;
        }
        if (count >= cap) break;

        raw_wl[count] = nums[0];
        raw_s0[count] = nums[1];
        raw_s1[count] = nums[2];
        raw_s2[count] = nums[3];
        count++;
    }

    if (fp) fclose(fp);

    if (alloc_failed || count <= 1) {
        free(raw_wl);
        free(raw_s0);
        free(raw_s1);
        free(raw_s2);
        return -1;
    }

    resample_linear(raw_wl, raw_s0, count,
                    g_ref.s0, CMF_WL_START, CMF_WL_COUNT);
    resample_linear(raw_wl, raw_s1, count,
                    g_ref.s1, CMF_WL_START, CMF_WL_COUNT);
    resample_linear(raw_wl, raw_s2, count,
                    g_ref.s2, CMF_WL_START, CMF_WL_COUNT);

    free(raw_wl);
    free(raw_s0);
    free(raw_s1);
    free(raw_s2);
    return 0;
}

static int load_ref_data(void) {
    if (g_ref.loaded) return 0;
    if (load_cmf() < 0) return -1;
    if (load_cri_reflectance() < 0) return -1;
    if (load_s_data() < 0) return -1;
    g_ref.loaded = 1;
    return 0;
}

static void compute_xyz_from_spd(const double *spd,
                                 double *X, double *Y, double *Z,
                                 double *K_out) {
    double sum_x = 0.0;
    double sum_y = 0.0;
    double sum_z = 0.0;
    for (int i = 0; i < CMF_WL_COUNT; i++) {
        double s = spd[i];
        sum_x += s * g_ref.xbar[i];
        sum_y += s * g_ref.ybar[i];
        sum_z += s * g_ref.zbar[i];
    }

    double K = (sum_y > 0.0) ? (100.0 / sum_y) : 0.0;
    if (X) *X = sum_x * K;
    if (Y) *Y = sum_y * K;
    if (Z) *Z = sum_z * K;
    if (K_out) *K_out = K;
}

static void compute_xyz_from_spd_reflectance(const double *spd,
                                             const double *refl,
                                             double K,
                                             double *X, double *Y,
                                             double *Z) {
    double sum_x = 0.0;
    double sum_y = 0.0;
    double sum_z = 0.0;
    for (int i = 0; i < CMF_WL_COUNT; i++) {
        double s = spd[i] * refl[i];
        sum_x += s * g_ref.xbar[i];
        sum_y += s * g_ref.ybar[i];
        sum_z += s * g_ref.zbar[i];
    }
    if (X) *X = sum_x * K;
    if (Y) *Y = sum_y * K;
    if (Z) *Z = sum_z * K;
}

static void xyz_to_xy(const double X, const double Y, const double Z,
                      double *x, double *y) {
    double sum = X + Y + Z;
    if (sum <= 0.0) {
        if (x) *x = 0.0;
        if (y) *y = 0.0;
        return;
    }
    if (x) *x = X / sum;
    if (y) *y = Y / sum;
}

static void xyz_to_uv(const double X, const double Y, const double Z,
                      double *u, double *v) {
    double denom = X + 15.0 * Y + 3.0 * Z;
    if (denom <= 0.0) {
        if (u) *u = 0.0;
        if (v) *v = 0.0;
        return;
    }
    if (u) *u = (4.0 * X) / denom;
    if (v) *v = (6.0 * Y) / denom;
}

static void uv_to_c_d(double u, double v, double *c, double *d) {
    if (v <= 0.0) {
        if (c) *c = 0.0;
        if (d) *d = 0.0;
        return;
    }
    if (c) *c = (4.0 - u - 10.0 * v) / v;
    if (d) *d = (1.708 * v + 0.404 - 1.481 * u) / v;
}

static void adapt_uv(double u_k, double v_k,
                     double u_n, double v_n,
                     double u_k1, double v_k1,
                     double *u_k1_p, double *v_k1_p) {
    double c_k = 0.0, d_k = 0.0;
    double c_n = 0.0, d_n = 0.0;
    double c_k1 = 0.0, d_k1 = 0.0;
    uv_to_c_d(u_k, v_k, &c_k, &d_k);
    uv_to_c_d(u_n, v_n, &c_n, &d_n);
    uv_to_c_d(u_k1, v_k1, &c_k1, &d_k1);

    if (c_k == 0.0 || d_k == 0.0) {
        if (u_k1_p) *u_k1_p = u_k1;
        if (v_k1_p) *v_k1_p = v_k1;
        return;
    }

    double c_k1p = c_n * (c_k1 / c_k);
    double d_k1p = d_n * (d_k1 / d_k);

    double denom = 16.518 + 1.481 * c_k1p - d_k1p;
    if (denom == 0.0) {
        if (u_k1_p) *u_k1_p = u_k1;
        if (v_k1_p) *v_k1_p = v_k1;
        return;
    }

    if (u_k1_p)
        *u_k1_p = (10.872 + 0.404 * c_k1p - 4.0 * d_k1p) / denom;
    if (v_k1_p)
        *v_k1_p = 5.520 / denom;
}

static void uv_to_wuv(double u, double v,
                      double u_n, double v_n,
                      double Y,
                      double *W, double *U, double *V) {
    double Yc = Y > 0.0 ? Y : 0.0;
    double Wv = 25.0 * pow(Yc, 1.0 / 3.0) - 17.0;
    if (W) *W = Wv;
    if (U) *U = 13.0 * Wv * (u - u_n);
    if (V) *V = 13.0 * Wv * (v - v_n);
}

static void blackbody_spd(double T, double *spd) {
    const double c2 = 1.4388e-2;
    for (int i = 0; i < CMF_WL_COUNT; i++) {
        double wl_nm = (double)(CMF_WL_START + i);
        double wl_m = wl_nm * 1e-9;
        double denom = exp(c2 / (wl_m * T)) - 1.0;
        if (denom <= 0.0) {
            spd[i] = 0.0;
        } else {
            spd[i] = 1.0 / (pow(wl_m, 5.0) * denom);
        }
    }
}

static void d_illuminant_spd(double T, double *spd) {
    double x_d = 0.0;
    if (T >= 4000.0 && T <= 7000.0) {
        x_d = (-4.6070e9 / (T * T * T))
            + (2.9678e6 / (T * T))
            + (0.09911e3 / T) + 0.244063;
    } else {
        x_d = (-2.0064e9 / (T * T * T))
            + (1.9018e6 / (T * T))
            + (0.24748e3 / T) + 0.237040;
    }
    double y_d = -3.0 * x_d * x_d + 2.870 * x_d - 0.275;
    double M = 0.0241 + 0.2562 * x_d - 0.7341 * y_d;
    double M1 = (-1.3515 - 1.7703 * x_d + 5.9114 * y_d) / M;
    double M2 = (0.0300 - 31.4424 * x_d + 30.0717 * y_d) / M;

    for (int i = 0; i < CMF_WL_COUNT; i++) {
        spd[i] = g_ref.s0[i] + M1 * g_ref.s1[i] + M2 * g_ref.s2[i];
    }
}

static double estimate_cct(double x, double y, double u, double v) {
    double n = (x - 0.3320) / (0.1858 - y);
    double cct = 449.0 * n * n * n + 3525.0 * n * n + 6823.3 * n + 5520.33;
    if (!(cct > 0.0 && cct < 1e6)) cct = 6500.0;

    double t_min = 1000.0;
    double t_max = 25000.0;
    if (cct < t_min) cct = t_min;
    if (cct > t_max) cct = t_max;

    double best_t = cct;
    double best_d = 1e9;

    double start = fmax(t_min, cct * 0.5);
    double end = fmin(t_max, cct * 1.5);
    double spd[CMF_WL_COUNT];
    for (double t = start; t <= end; t += 100.0) {
        double X = 0.0, Y = 0.0, Z = 0.0;
        blackbody_spd(t, spd);
        compute_xyz_from_spd(spd, &X, &Y, &Z, NULL);
        double uu = 0.0, vv = 0.0;
        xyz_to_uv(X, Y, Z, &uu, &vv);
        double d = hypot(u - uu, v - vv);
        if (d < best_d) {
            best_d = d;
            best_t = t;
        }
    }

    double refine = 10.0;
    for (int pass = 0; pass < 2; pass++) {
        double t0 = fmax(t_min, best_t - 100.0 * refine / 10.0);
        double t1 = fmin(t_max, best_t + 100.0 * refine / 10.0);
        for (double t = t0; t <= t1; t += refine) {
            double X = 0.0, Y = 0.0, Z = 0.0;
            blackbody_spd(t, spd);
            compute_xyz_from_spd(spd, &X, &Y, &Z, NULL);
            double uu = 0.0, vv = 0.0;
            xyz_to_uv(X, Y, Z, &uu, &vv);
            double d = hypot(u - uu, v - vv);
            if (d < best_d) {
                best_d = d;
                best_t = t;
            }
        }
        refine = 1.0;
    }

    return best_t;
}

static int fill_spd_from_ctx(const SpectrometerContext *ctx, double *spd) {
    if (!ctx || !spd || ctx->src_dx <= 1 || ctx->kx <= 0.0f) return -1;
    for (int i = 0; i < CMF_WL_COUNT; i++) {
        double nm = (double)(CMF_WL_START + i);
        if (nm < ctx->nm_start || nm > ctx->nm_end) {
            spd[i] = 0.0;
            continue;
        }
        double x = (nm - ctx->nm_start) * ctx->nm_coeff;
        double bin_f = x / ctx->kx;
        if (bin_f < 0.0 || bin_f > (double)(ctx->src_dx - 1)) {
            spd[i] = 0.0;
            continue;
        }
        int b0 = (int)floor(bin_f);
        int b1 = b0 + 1;
        if (b0 < 0) b0 = 0;
        if (b1 >= ctx->src_dx) b1 = ctx->src_dx - 1;
        double t = bin_f - b0;
        double v0 = ctx->spec_array_irradiance[b0];
        double v1 = ctx->spec_array_irradiance[b1];
        spd[i] = v0 + (v1 - v0) * t;
        if (spd[i] < 0.0) spd[i] = 0.0;
    }
    return 0;
}

static double spd_sample_from_csv(void *user, double nm) {
    const SpectrumData *data = (const SpectrumData *)user;
    return csv_spectrum_sample(data, nm);
}

static int compute_cri_from_spd(const double *spd_test, CRIResult *out) {
    if (load_ref_data() < 0) return -1;

    double Xk = 0.0, Yk = 0.0, Zk = 0.0;
    double Kk = 0.0;
    compute_xyz_from_spd(spd_test, &Xk, &Yk, &Zk, &Kk);
    if (Kk == 0.0) return -1;

    double x = 0.0, y = 0.0;
    xyz_to_xy(Xk, Yk, Zk, &x, &y);
    double u_k = 0.0, v_k = 0.0;
    xyz_to_uv(Xk, Yk, Zk, &u_k, &v_k);

    double cct = estimate_cct(x, y, u_k, v_k);
    double spd_ref[CMF_WL_COUNT];
    if (cct < 5000.0) {
        double T = cct * 1.000556;
        blackbody_spd(T, spd_ref);
    } else {
        d_illuminant_spd(cct, spd_ref);
    }

    double Xn = 0.0, Yn = 0.0, Zn = 0.0;
    double Kn = 0.0;
    compute_xyz_from_spd(spd_ref, &Xn, &Yn, &Zn, &Kn);
    if (Kn == 0.0) return -1;

    double u_n = 0.0, v_n = 0.0;
    xyz_to_uv(Xn, Yn, Zn, &u_n, &v_n);

    if (out) {
        out->cct = cct;
        out->ri_count = CRI_SAMPLE_COUNT;
    }

    double ra_sum = 0.0;

    for (int i = 0; i < CRI_SAMPLE_COUNT; i++) {
        double Xk1 = 0.0, Yk1 = 0.0, Zk1 = 0.0;
        double Xn1 = 0.0, Yn1 = 0.0, Zn1 = 0.0;
        compute_xyz_from_spd_reflectance(spd_test, g_ref.r[i], Kk,
                                         &Xk1, &Yk1, &Zk1);
        compute_xyz_from_spd_reflectance(spd_ref, g_ref.r[i], Kn,
                                         &Xn1, &Yn1, &Zn1);

        double u_k1 = 0.0, v_k1 = 0.0;
        double u_n1 = 0.0, v_n1 = 0.0;
        xyz_to_uv(Xk1, Yk1, Zk1, &u_k1, &v_k1);
        xyz_to_uv(Xn1, Yn1, Zn1, &u_n1, &v_n1);

        double u_k1p = 0.0, v_k1p = 0.0;
        adapt_uv(u_k, v_k, u_n, v_n, u_k1, v_k1, &u_k1p, &v_k1p);

        double Wk = 0.0, Uk = 0.0, Vk = 0.0;
        double Wn = 0.0, Un = 0.0, Vn = 0.0;
        uv_to_wuv(u_k1p, v_k1p, u_n, v_n, Yk1, &Wk, &Uk, &Vk);
        uv_to_wuv(u_n1, v_n1, u_n, v_n, Yn1, &Wn, &Un, &Vn);

        double dE = sqrt((Wk - Wn) * (Wk - Wn)
                       + (Uk - Un) * (Uk - Un)
                       + (Vk - Vn) * (Vk - Vn));
        double Ri = 100.0 - 4.6 * dE;
        if (out) out->ri[i] = Ri;
        if (i < 8) ra_sum += Ri;
    }

    if (out) out->ra = ra_sum / 8.0;
    return 0;
}

int colorimetry_compute_cri_from_ctx(const SpectrometerContext *ctx,
                                     CRIResult *out) {
    double spd[CMF_WL_COUNT];
    if (fill_spd_from_ctx(ctx, spd) < 0) return -1;
    return compute_cri_from_spd(spd, out);
}

int colorimetry_compute_cri_from_csv(const char *csv_path,
                                     CRIResult *out) {
    if (!csv_path) return -1;
    SpectrumData data;
    if (csv_read_spectrum(csv_path, &data) < 0) return -1;

    double spd[CMF_WL_COUNT];
    for (int i = 0; i < CMF_WL_COUNT; i++) {
        double nm = (double)(CMF_WL_START + i);
        spd[i] = spd_sample_from_csv(&data, nm);
        if (spd[i] < 0.0) spd[i] = 0.0;
    }

    int ret = compute_cri_from_spd(spd, out);
    csv_free_spectrum(&data);
    return ret;
}

void colorimetry_print_result(const CRIResult *res) {
    if (!res) return;
    printf("CCT: %.0f K\n", res->cct);
    printf("Ra: %.2f\n", res->ra);
    printf("R1-R8: ");
    for (int i = 0; i < 8 && i < res->ri_count; i++) {
        printf("R%d=%.2f%s", i + 1, res->ri[i], i == 7 ? "\n" : " ");
    }
    if (res->ri_count > 8) {
        printf("R9-R15: ");
        for (int i = 8; i < res->ri_count; i++) {
            printf("R%d=%.2f%s", i + 1, res->ri[i],
                   i == res->ri_count - 1 ? "\n" : " ");
        }
    }
}

void colorimetry_set_use_external_refs(int use_external) {
    g_use_external_refs = use_external ? 1 : 0;
    g_ref.loaded = 0;
}
