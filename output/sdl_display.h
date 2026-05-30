#ifndef SDL_DISPLAY_H
#define SDL_DISPLAY_H

#include "../lib/spectrometer.h"
#include "../lib/wavelength.h"
#include <stdint.h>

#define SDL_KEYMASK_SAVE 0x1u
#define SDL_KEYMASK_EXPOSURE_DEC 0x2u
#define SDL_KEYMASK_EXPOSURE_INC 0x4u
#define SDL_KEYMASK_CALIBRATE 0x8u
#define SDL_KEYMASK_COLORIMETRY 0x10u

#define COLORIMETRY_UPDATE_SYNC 0
#define COLORIMETRY_UPDATE_FIXED 1
#define COLORIMETRY_UPDATE_MODE COLORIMETRY_UPDATE_SYNC
#define COLORIMETRY_FIXED_INTERVAL_MS 500

#define MAX_PEAKS 10
#define PEAK_RATIO 0.05f

typedef struct {
    void *window;
    void *renderer;
    void *texture;
    int width;
    int height;
    int spectrum_height;
    int running;
    uint32_t key_mask;
    uint8_t *last_frame;
    int last_frame_len;
    int show_colorimetry;
    int colorimetry_valid;
    double cct;
    double ra;
    uint32_t last_colorimetry_ms;
} SDLDisplay;

int  sdl_display_init(SDLDisplay *dpy, int width, int spectrum_h);
void sdl_display_render(SDLDisplay *dpy, const SpectrometerContext *ctx);
int  sdl_display_save_ppm(SDLDisplay *dpy, const char *path);
void sdl_display_close(SDLDisplay *dpy);

#endif
