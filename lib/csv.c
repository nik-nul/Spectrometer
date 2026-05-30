#include "csv.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static float calc_percentual(const SpectrometerContext *ctx, float value) {
    if (ctx->reference_scale) {
        return value * 110.0f / ctx->max_value;
    }
    return value;
}

int csv_write_spectrum_fp(FILE *fp, const SpectrometerContext *ctx,
                          char sep) {
    if (!fp || !ctx) return -1;

    if (sep == '\t') {
        fprintf(fp, "Nanometers%cIntensity(a.u.)\n", sep);
    } else {
        fprintf(fp, "Nanometers%cIntensity(a.u.)\n", sep);
    }

    int i;
    for (i = 0; i < ctx->src_dx; i++) {
        float nm = spec_x_to_nanometers(ctx, spec_bin_to_x(ctx, i));
        float intensity = calc_percentual(ctx,
                                         ctx->spec_array_irradiance[i]);
        fprintf(fp, "%.1f%c%.1f\n", nm, sep, intensity);
    }
    return 0;
}

int csv_write_spectrum(const char *filename,
                       const SpectrometerContext *ctx,
                       char separator) {
    if (!filename || !ctx) return -1;
    FILE *fp = fopen(filename, "w");
    if (!fp) return -1;
    int ret = csv_write_spectrum_fp(fp, ctx, separator);
    fclose(fp);
    return ret;
}

int csv_read_spectrum(const char *filename, SpectrumData *out) {
    if (!filename || !out) return -1;
    memset(out, 0, sizeof(*out));

    FILE *fp = fopen(filename, "r");
    if (!fp) return -1;

    int cap = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '#') continue;

        char *end = NULL;
        double nm = strtod(p, &end);
        if (end == p) continue;
        p = end;
        while (*p && (*p == ',' || *p == ';' || isspace((unsigned char)*p))) p++;
        double val = strtod(p, &end);
        if (end == p) continue;

        if (out->count >= cap) {
            int new_cap = cap > 0 ? cap * 2 : 256;
            double *new_nm = (double *)realloc(out->nm, sizeof(double) * new_cap);
            double *new_val = (double *)realloc(out->value, sizeof(double) * new_cap);
            if (!new_nm || !new_val) {
                free(new_nm);
                free(new_val);
                fclose(fp);
                csv_free_spectrum(out);
                return -1;
            }
            out->nm = new_nm;
            out->value = new_val;
            cap = new_cap;
        }

        out->nm[out->count] = nm;
        out->value[out->count] = val;
        out->count++;
    }

    fclose(fp);
    if (out->count < 2) {
        csv_free_spectrum(out);
        return -1;
    }
    return 0;
}

void csv_free_spectrum(SpectrumData *data) {
    if (!data) return;
    free(data->nm);
    free(data->value);
    data->nm = NULL;
    data->value = NULL;
    data->count = 0;
}

double csv_spectrum_sample(const SpectrumData *data, double nm) {
    if (!data || data->count < 2) return 0.0;
    if (nm < data->nm[0] || nm > data->nm[data->count - 1]) return 0.0;

    int lo = 0;
    int hi = data->count - 1;
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (data->nm[mid] <= nm) lo = mid;
        else hi = mid;
    }

    double w0 = data->nm[lo];
    double w1 = data->nm[hi];
    double v0 = data->value[lo];
    double v1 = data->value[hi];
    if (w1 <= w0) return v0;
    double t = (nm - w0) / (w1 - w0);
    return v0 + (v1 - v0) * t;
}
