#include "calibration.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

int calibration_load(const char *path,
                     float *nm_start,
                     float *nm_per_x,
                     CalibrationPoint *points,
                     int max_points,
                     int *out_count) {
    if (!path || !points || max_points <= 0) return -1;

    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[128];
    int count = 0;
    int has_start = 0;
    int has_per_x = 0;
    float start_val = 0.0f;
    float per_x_val = 0.0f;

    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1;
    }

    if (strncmp(line, "CALIBRATION_V1", 14) != 0) {
        fclose(fp);
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#') continue;
        if (line[0] == '\n' || line[0] == '\r') continue;

        if (sscanf(line, "nm_start %f", &start_val) == 1) {
            has_start = 1;
            continue;
        }
        if (sscanf(line, "nm_per_x %f", &per_x_val) == 1) {
            has_per_x = 1;
            continue;
        }
        if (sscanf(line, "points %*d") == 1) {
            continue;
        }

        if (count < max_points) {
            int bin = 0;
            float nm = 0.0f;
            if (sscanf(line, "%d %f", &bin, &nm) == 2) {
                points[count].bin = bin;
                points[count].nm = nm;
                count++;
            }
        }
    }

    fclose(fp);

    if (out_count) *out_count = count;
    if (nm_start) *nm_start = has_start ? start_val : 0.0f;
    if (nm_per_x) *nm_per_x = has_per_x ? per_x_val : 0.0f;
    return 0;
}

int calibration_save(const char *path,
                     float nm_start,
                     float nm_per_x,
                     const CalibrationPoint *points,
                     int count) {
    if (!path || !points || count < 0) return -1;

    FILE *fp = fopen(path, "w");
    if (!fp) return -1;

    fprintf(fp, "CALIBRATION_V1\n");
    fprintf(fp, "nm_start %.6f\n", nm_start);
    fprintf(fp, "nm_per_x %.6f\n", nm_per_x);
    fprintf(fp, "points %d\n", count);
    for (int i = 0; i < count; i++) {
        fprintf(fp, "%d %.6f\n", points[i].bin, points[i].nm);
    }

    fclose(fp);
    return 0;
}

int calibration_compute(const SpectrometerContext *ctx,
                        const CalibrationPoint *points,
                        int count,
                        float *nm_start,
                        float *nm_per_x) {
    if (!ctx || !points || count < 2 || !nm_start || !nm_per_x) return -1;

    double sum_x = 0.0;
    double sum_y = 0.0;
    double sum_xx = 0.0;
    double sum_xy = 0.0;

    for (int i = 0; i < count; i++) {
        float x = spec_bin_to_x(ctx, points[i].bin);
        double xd = (double)x;
        double yd = (double)points[i].nm;
        sum_x += xd;
        sum_y += yd;
        sum_xx += xd * xd;
        sum_xy += xd * yd;
    }

    double n = (double)count;
    double denom = n * sum_xx - sum_x * sum_x;
    if (fabs(denom) < 1e-9) return -1;

    double b = (n * sum_xy - sum_x * sum_y) / denom;
    double a = (sum_y - b * sum_x) / n;
    if (b <= 0.0) return -1;

    *nm_start = (float)a;
    *nm_per_x = (float)b;
    return 0;
}
