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
#define SDL_KEYMASK_PAUSE 0x20u
#define SDL_KEYMASK_GAMUT_SAMPLE 0x40u
#define SDL_KEYMASK_GAMUT_RESET 0x80u

#define COLORIMETRY_UPDATE_SYNC 0
#define COLORIMETRY_UPDATE_FIXED 1
#define COLORIMETRY_UPDATE_MODE COLORIMETRY_UPDATE_SYNC
#define COLORIMETRY_FIXED_INTERVAL_MS 500

#define GAMUT_STAGE_COUNT 3
#define GAMUT_REF_COUNT 4

#define MAX_PEAKS 10
#define PEAK_RATIO 0.05f

typedef struct {
    double area_ratio;
    double coverage_ratio;
} GamutMetrics;

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
    int gamut_mode;
    void *gamut_window;
    void *gamut_renderer;
    uint32_t gamut_window_id;
    int gamut_width;
    int gamut_height;
    int gamut_stage;
    int gamut_samples;
    double gamut_xy[GAMUT_STAGE_COUNT][2];
    int gamut_metrics_valid;
    GamutMetrics gamut_metrics[GAMUT_REF_COUNT];
} SDLDisplay;

int  sdl_display_init(SDLDisplay *dpy, int width, int spectrum_h);
void sdl_display_render(SDLDisplay *dpy, const SpectrometerContext *ctx);
int  sdl_display_save_ppm(SDLDisplay *dpy, const char *path);
int  sdl_display_enable_gamut_mode(SDLDisplay *dpy, int width, int height);
void sdl_display_gamut_reset(SDLDisplay *dpy);
int  sdl_display_gamut_sample(SDLDisplay *dpy,
                              const SpectrometerContext *ctx,
                              int *out_stage,
                              double *out_x,
                              double *out_y);
void sdl_display_close(SDLDisplay *dpy);

#endif
