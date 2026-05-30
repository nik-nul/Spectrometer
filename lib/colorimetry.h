#ifndef COLORIMETRY_H
#define COLORIMETRY_H

#include "spectrometer.h"

#define CRI_SAMPLE_COUNT 15

typedef struct {
    double cct;
    double ra;
    double ri[CRI_SAMPLE_COUNT];
    int ri_count;
} CRIResult;

int colorimetry_compute_cri_from_ctx(const SpectrometerContext *ctx,
                                     CRIResult *out);
int colorimetry_compute_cri_from_csv(const char *csv_path,
                                     CRIResult *out);
void colorimetry_set_use_external_refs(int use_external);
void colorimetry_print_result(const CRIResult *res);

#endif
