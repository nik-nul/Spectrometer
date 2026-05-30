#ifndef CSV_H
#define CSV_H

#include <stdio.h>
#include "spectrometer.h"

typedef struct {
    double *nm;
    double *value;
    int count;
} SpectrumData;

int csv_write_spectrum(const char *filename,
                       const SpectrometerContext *ctx,
                       char separator);

int csv_write_spectrum_fp(FILE *fp,
                          const SpectrometerContext *ctx,
                          char separator);

int csv_read_spectrum(const char *filename, SpectrumData *out);
void csv_free_spectrum(SpectrumData *data);
double csv_spectrum_sample(const SpectrumData *data, double nm);

#endif
