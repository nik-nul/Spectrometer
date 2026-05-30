#include "wavelength.h"
#include <math.h>

ColorRGB wavelength_to_color(float wl) {
    float r = 0.0f, g = 0.0f, b = 0.0f;
    float factor = 1.0f;

    if (wl >= 380.0f && wl < 440.0f) {
        r = -(wl - 440.0f) / (440.0f - 380.0f);
        g = 0.0f;
        b = 1.0f;
    } else if (wl >= 440.0f && wl < 490.0f) {
        r = 0.0f;
        g = (wl - 440.0f) / (490.0f - 440.0f);
        b = 1.0f;
    } else if (wl >= 490.0f && wl < 510.0f) {
        r = 0.0f;
        g = 1.0f;
        b = -(wl - 510.0f) / (510.0f - 490.0f);
    } else if (wl >= 510.0f && wl < 580.0f) {
        r = (wl - 510.0f) / (580.0f - 510.0f);
        g = 1.0f;
        b = 0.0f;
    } else if (wl >= 580.0f && wl < 645.0f) {
        r = 1.0f;
        g = -(wl - 645.0f) / (645.0f - 580.0f);
        b = 0.0f;
    } else if (wl >= 645.0f && wl <= 780.0f) {
        r = 1.0f;
        g = 0.0f;
        b = 0.0f;
    } else {
        r = 0.0f; g = 0.0f; b = 0.0f;
    }

    const float UV1 = 380.0f, UV2 = 420.0f;
    const float IR1 = 650.0f, IR2 = 780.0f;

    if (wl > IR2 || wl < UV1) {
        factor = 0.0f;
    } else if (wl > IR1) {
        factor = (IR2 - wl) / (IR2 - IR1);
    } else if (wl < UV2) {
        factor = (wl - UV1) / (UV2 - UV1);
    } else {
        factor = 1.0f;
    }

    ColorRGB c;
    c.r = (uint8_t)(fminf(255.0f, r * factor * 255.0f));
    c.g = (uint8_t)(fminf(255.0f, g * factor * 255.0f));
    c.b = (uint8_t)(fminf(255.0f, b * factor * 255.0f));
    return c;
}
