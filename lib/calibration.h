#ifndef CALIBRATION_H
#define CALIBRATION_H

#include "spectrometer.h"

typedef struct {
    int bin;
    float nm;
} CalibrationPoint;

int calibration_load(const char *path,
                     float *nm_start,
                     float *nm_per_x,
                     CalibrationPoint *points,
                     int max_points,
                     int *out_count);

int calibration_save(const char *path,
                     float nm_start,
                     float nm_per_x,
                     const CalibrationPoint *points,
                     int count);

int calibration_compute(const SpectrometerContext *ctx,
                        const CalibrationPoint *points,
                        int count,
                        float *nm_start,
                        float *nm_per_x);

#endif
