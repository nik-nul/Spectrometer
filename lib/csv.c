#include "csv.h"
#include <stdio.h>
#include <string.h>

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
