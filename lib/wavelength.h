#ifndef WAVELENGTH_H
#define WAVELENGTH_H

#include <stdint.h>

typedef struct {
    uint8_t r, g, b;
} ColorRGB;

ColorRGB wavelength_to_color(float wavelength_nm);

#endif
