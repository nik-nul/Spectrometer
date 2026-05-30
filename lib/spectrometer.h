#ifndef SPECTROMETER_H
#define SPECTROMETER_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_BINS 4096
#define IRRD_COEFF_COUNT 401
#define IRRD_WAVELENGTH_START 380
#define IRRD_WAVELENGTH_END 780

typedef struct {
    int start_x;     // 0-1000
    int end_x;       // 0-1000
    int start_y;     // 0-100
    int size_y;      // 0-100
    int filter;      // 0-100
    int rising_speed; // 0-100
    int falling_speed; // 0-100
    float nm_min;
    float nm_max;
    float trim_point1;
    float trim_point2;
    bool flip;
    bool show_peaks;
    bool show_dips;
    bool use_colors;
    bool trim_scale;
    bool reference_enabled;
    bool gamma_enable;
    float gamma;
    int dest_width;
    int dest_height;
} SpectrometerConfig;

typedef struct {
    int src_w;
    int src_h;
    int src_x0;
    int src_dx;
    int src_y0;
    int src_dy;
    float kred;
    float kgreen;
    float kblue;
    float kfilter;
    float kspeed_up;
    float kspeed_dw;
    float nm_start;
    float nm_end;
    float nm_delta;
    float nm_coeff;    // dest_width / (nm_end - nm_start)
    bool flip;
    bool show_peaks;
    bool gamma_enable;
    float gamma;
    uint16_t gamma_lut[256];
    float bg_noise;
    float kx;          // dest_width / (src_dx - 1)
    float ky;          // (dest_bottom - 15) / max_v
    int dest_width;
    int dest_height;
    int dest_right;
    int dest_bottom;
    float max_v;

    float spec_v[MAX_BINS];
    float spec_array[MAX_BINS];
    float spec_array_filtered[MAX_BINS];
    float spec_array_irradiance[MAX_BINS];
    float reference[MAX_BINS];
    int reference_len;
    bool reference_scale;

    float max_value;
    int max_value_x;

    float irradiance_coeff[IRRD_COEFF_COUNT];
    bool irradiance_initialized;
} SpectrometerContext;

void spec_init_config(SpectrometerConfig *cfg);
int spec_init_context(SpectrometerContext *ctx, const SpectrometerConfig *cfg);
void spec_set_source_params(SpectrometerContext *ctx, int src_w, int src_h,
                            const SpectrometerConfig *cfg);
void spec_set_running_mode_params(SpectrometerContext *ctx,
                                  const SpectrometerConfig *cfg);
void spec_set_running_mode_params_fps(SpectrometerContext *ctx,
                                      const SpectrometerConfig *cfg,
                                      float fps,
                                      float target_fps);
void spec_set_scale_trim_params(SpectrometerContext *ctx,
                                const SpectrometerConfig *cfg);
int  spec_apply_calibration(SpectrometerContext *ctx,
                            float nm_start,
                            float nm_per_x);

void spec_process_frame(SpectrometerContext *ctx,
                        const uint8_t *rgb24_data,
                        int src_w, int src_h);

void spec_set_reference(SpectrometerContext *ctx);
void spec_reset_reference(SpectrometerContext *ctx);

float spec_x_to_nanometers(const SpectrometerContext *ctx, float x);
float spec_x_from_nanometers(const SpectrometerContext *ctx, float nm);
float spec_bin_to_x(const SpectrometerContext *ctx, int bin);
int   spec_x_to_bin(const SpectrometerContext *ctx, float x);

int  spec_find_peaks(const SpectrometerContext *ctx, int *peaks_out,
                     int max_peaks);
int  spec_collect_peaks(const SpectrometerContext *ctx, int *peaks_out,
                        float *values_out, int max_peaks);
int  spec_find_dips(const SpectrometerContext *ctx, int *dips_out,
                    int max_dips);

#endif
