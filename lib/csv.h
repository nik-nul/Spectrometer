#ifndef CSV_H
#define CSV_H

#include <stdio.h>
#include "spectrometer.h"

int csv_write_spectrum(const char *filename,
                       const SpectrometerContext *ctx,
                       char separator);

int csv_write_spectrum_fp(FILE *fp,
                          const SpectrometerContext *ctx,
                          char separator);

#endif
