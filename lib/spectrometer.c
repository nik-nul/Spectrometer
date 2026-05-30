#include "spectrometer.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

static const float UV380_COEFF = 2.568f;
static const float IR780_COEFF = 1.41f;
static const float AVG_BG_NOISE = 2.2f;
static const float MAX_V_DEFAULT = 800.0f;

// static const float DEFAULT_IRRADIANCE[IRRD_COEFF_COUNT];

void spec_init_config(SpectrometerConfig *cfg) {
    cfg->start_x = 0;
    cfg->end_x = 1000;
    cfg->start_y = 45;
    cfg->size_y = 10;
    cfg->filter = 30;
    cfg->rising_speed = 30;
    cfg->falling_speed = 30;
    cfg->nm_min = 270.0f;
    cfg->nm_max = 1200.0f;
    cfg->trim_point1 = 436.0f;
    cfg->trim_point2 = 546.0f;
    cfg->flip = true;
    cfg->show_peaks = true;
    cfg->show_dips = false;
    cfg->use_colors = true;
    cfg->trim_scale = false;
    cfg->reference_enabled = false;
    cfg->gamma_enable = true;
    cfg->gamma = 2.2f;
    cfg->dest_width = 1920;
    cfg->dest_height = 880;
}

static void build_gamma_lut(uint16_t *lut, bool enable, float gamma) {
    if (!lut) return;
    if (!enable || gamma <= 0.0f) {
        for (int i = 0; i < 256; i++) {
            lut[i] = (uint16_t)i;
        }
        return;
    }

    for (int i = 0; i < 256; i++) {
        float v = (float)i / 255.0f;
        float lin = powf(v, gamma);
        int out = (int)(lin * 255.0f + 0.5f);
        if (out < 0) out = 0;
        if (out > 255) out = 255;
        lut[i] = (uint16_t)out;
    }
}

static float adjust_bg_noise(bool enable, float gamma) {
    if (!enable || gamma <= 0.0f) return AVG_BG_NOISE;

    float v = AVG_BG_NOISE / 255.0f;
    float lin = powf(v, gamma);
    return lin * 255.0f;
}

int spec_init_context(SpectrometerContext *ctx,
                      const SpectrometerConfig *cfg) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->max_v = MAX_V_DEFAULT;
    ctx->dest_width = cfg->dest_width;
    ctx->dest_height = cfg->dest_height;
    ctx->dest_right = cfg->dest_width - 1;
    ctx->dest_bottom = cfg->dest_height - 2;
    int i;
    for (i = 0; i < IRRD_COEFF_COUNT; i++) {
        ctx->irradiance_coeff[i] = 1;
    }
    ctx->gamma_enable = cfg->gamma_enable;
    ctx->gamma = cfg->gamma;
    build_gamma_lut(ctx->gamma_lut, ctx->gamma_enable, ctx->gamma);
    ctx->bg_noise = adjust_bg_noise(ctx->gamma_enable, ctx->gamma);
    ctx->irradiance_initialized = true;
    return 0;
}

void spec_set_source_params(SpectrometerContext *ctx,
                            int src_w, int src_h,
                            const SpectrometerConfig *cfg) {
    ctx->src_w = src_w;
    ctx->src_h = src_h;

    int start_x = cfg->start_x;
    int end_x   = cfg->end_x;
    int start_y = cfg->start_y;
    int size_y  = cfg->size_y;

    ctx->src_x0 = (src_w * start_x) / 1000;
    ctx->src_dx = src_w - ctx->src_x0
                + (src_w * (end_x - 1000)) / 1000;
    ctx->src_dy = (src_h * size_y) / 100;
    ctx->src_y0 = src_h - (src_h * start_y / 100) - ctx->src_dy;

    if (ctx->src_x0 + ctx->src_dx > src_w)
        ctx->src_dx = src_w - ctx->src_x0;
    if (ctx->src_dx <= 0) {
        ctx->src_x0 += (ctx->src_dx - 1);
        ctx->src_dx = 1;
    }
    if (ctx->src_x0 < 0) ctx->src_x0 = 0;
    if (ctx->src_y0 + ctx->src_dy > src_h)
        ctx->src_dy = src_h - ctx->src_y0;
    if (ctx->src_dy <= 0) {
        ctx->src_y0 += (ctx->src_dy - 1);
        ctx->src_dy = 1;
    }
    if (ctx->src_y0 < 0) ctx->src_y0 = 0;

    int bins = ctx->src_dx;
    if (bins > MAX_BINS) bins = MAX_BINS;
    ctx->src_dx = bins;

    ctx->kred   = 1.0f / ctx->src_dy;
    ctx->kgreen = 1.0f / ctx->src_dy;
    ctx->kblue  = 1.0f / ctx->src_dy;

    spec_set_scale_trim_params(ctx, cfg);
    spec_reset_reference(ctx);
}

void spec_set_running_mode_params(SpectrometerContext *ctx,
                                  const SpectrometerConfig *cfg) {
    ctx->kfilter   = (100.0f - cfg->filter) / 100.0f + 0.1f;
    ctx->kspeed_up = cfg->rising_speed / 100.0f;
    ctx->kspeed_dw = cfg->falling_speed / 100.0f;
    ctx->show_peaks = cfg->show_peaks;
}

static float adjust_rate(float base, float ratio) {
    if (ratio <= 0.0f) return base;
    if (base <= 0.0f) return 0.0f;
    if (base >= 1.0f) return base;
    return 1.0f - powf(1.0f - base, ratio);
}

void spec_set_running_mode_params_fps(SpectrometerContext *ctx,
                                      const SpectrometerConfig *cfg,
                                      float fps,
                                      float target_fps) {
    if (fps <= 0.0f || target_fps <= 0.0f) {
        spec_set_running_mode_params(ctx, cfg);
        return;
    }

    float ratio = target_fps / fps;
    float base_filter = (100.0f - cfg->filter) / 100.0f + 0.1f;
    float base_up = cfg->rising_speed / 100.0f;
    float base_dw = cfg->falling_speed / 100.0f;

    ctx->kfilter = adjust_rate(base_filter, ratio);
    ctx->kspeed_up = adjust_rate(base_up, ratio);
    ctx->kspeed_dw = adjust_rate(base_dw, ratio);
    ctx->show_peaks = cfg->show_peaks;
}

void spec_set_scale_trim_params(SpectrometerContext *ctx,
                                const SpectrometerConfig *cfg) {
    float nm_min = cfg->nm_min;
    float nm_max = cfg->nm_max;
    if (nm_min < 50.0f) nm_min = 50.0f;
    if (nm_max > 4000.0f) nm_max = 4000.0f;
    if (nm_max < nm_min + 20.0f) nm_max = nm_min + 20.0f;

    ctx->nm_delta = nm_max - nm_min;

    float src_w_f = (float)ctx->src_w;
    ctx->nm_start = nm_min + ctx->nm_delta * ctx->src_x0 / src_w_f;
    float end = (float)(ctx->src_x0 + ctx->src_dx - 1) / src_w_f;
    ctx->nm_end   = nm_max - ctx->nm_delta * (1.0f - end);

    if (ctx->nm_end > ctx->nm_start)
        ctx->nm_coeff = ctx->dest_width
                      / (ctx->nm_end - ctx->nm_start);
    else
        ctx->nm_coeff = 1.0f;

    ctx->flip = cfg->flip;

    ctx->kx = (float)ctx->dest_right
            / (float)(ctx->src_dx - 1 < 1 ? 1 : ctx->src_dx - 1);
    ctx->ky = (ctx->dest_bottom - 15.0f) / ctx->max_v;
}

int spec_apply_calibration(SpectrometerContext *ctx,
                           float nm_start,
                           float nm_per_x) {
    if (!ctx || nm_per_x <= 0.0f) return -1;

    ctx->nm_start = nm_start;
    ctx->nm_coeff = 1.0f / nm_per_x;
    ctx->nm_end = nm_start + nm_per_x * ctx->dest_right;
    ctx->nm_delta = ctx->nm_end - nm_start;
    return 0;
}

void spec_process_frame(SpectrometerContext *ctx,
                        const uint8_t *rgb24_data,
                        int src_w, int src_h) {
    if (!rgb24_data) return;

    if (src_w != ctx->src_w || src_h != ctx->src_h) {
        return;
    }

    int x, y;
    int stride = src_w * 3;

    for (x = 0; x < ctx->src_dx; x++) {
        int sum_r = 0, sum_g = 0, sum_b = 0;
        int src_x = ctx->flip ? (ctx->src_dx - 1 - x) : x;
        int col_off = (ctx->src_x0 + src_x) * 3;

        for (y = 0; y < ctx->src_dy; y++) {
            int offset = (ctx->src_y0 + y) * stride + col_off;
            sum_r += ctx->gamma_lut[rgb24_data[offset + 2]];
            sum_g += ctx->gamma_lut[rgb24_data[offset + 1]];
            sum_b += ctx->gamma_lut[rgb24_data[offset]];
        }

        float v = sum_r * ctx->kred
                + sum_g * ctx->kgreen
                + sum_b * ctx->kblue;

        ctx->spec_v[x] = v;

        if (v > ctx->spec_array[x]) {
            ctx->spec_array[x] += (v - ctx->spec_array[x])
                                * ctx->kspeed_up;
        } else {
            ctx->spec_array[x] += (v - ctx->spec_array[x])
                                * ctx->kspeed_dw;
        }
    }

    ctx->max_value = 80.0f;
    ctx->max_value_x = 0;

    int i;
    float v_acc = 0.0f;
    for (i = 0; i < ctx->src_dx; i++) {
        float vnew = ctx->spec_array[i];
        v_acc += (vnew - v_acc) * ctx->kfilter;
        ctx->spec_array_filtered[i] = v_acc;
    }

    v_acc = 0.0f;
    for (i = ctx->src_dx - 1; i >= 0; i--) {
        float vnew = ctx->spec_array[i];
        v_acc += (vnew - v_acc) * ctx->kfilter;
        ctx->spec_array_filtered[i] = v_acc;

        if (v_acc > ctx->max_value) {
            ctx->max_value = v_acc;
            ctx->max_value_x = i;
        }
    }

    if (ctx->reference_scale && ctx->reference_len > 0) {
        int ref_len = ctx->reference_len < ctx->src_dx
                    ? ctx->reference_len : ctx->src_dx;
        for (i = 0; i < ref_len; i++) {
            float ref = ctx->reference[i];
            if (ref < 1.0f) ref = 99999.0f;
            float v = ctx->spec_array_filtered[i];
            v = v * 0.9f * ctx->max_value / ref;
            if (v > ctx->max_value) v = ctx->max_value;
            ctx->spec_array_filtered[i] = v;
        }
    }

    for (i = 0; i < ctx->src_dx; i++) {
        float nm = spec_x_to_nanometers(ctx, spec_bin_to_x(ctx, i));
        int nm_int = (int)nearbyintf(nm);
        float val = ctx->spec_array_filtered[i];
        float corrected;

        if (val - ctx->bg_noise >= 0) {
            if (nm_int < IRRD_WAVELENGTH_START) {
                corrected = (val - ctx->bg_noise) * UV380_COEFF;
            } else if (nm_int <= IRRD_WAVELENGTH_END) {
                int idx = nm_int - IRRD_WAVELENGTH_START;
                if (idx < 0) idx = 0;
                if (idx >= IRRD_COEFF_COUNT)
                    idx = IRRD_COEFF_COUNT - 1;
                corrected = (val - ctx->bg_noise)
                          * ctx->irradiance_coeff[idx];
            } else {
                corrected = val * IR780_COEFF;
            }
            if (corrected > ctx->max_v)
                corrected = ctx->max_v;
        } else {
            corrected = 0.0f;
        }
        ctx->spec_array_irradiance[i] = corrected;
    }
}

void spec_set_reference(SpectrometerContext *ctx) {
    int len = ctx->src_dx;
    if (len > MAX_BINS) len = MAX_BINS;
    int i;
    for (i = 0; i < len; i++) {
        ctx->reference[i] = ctx->spec_array_filtered[i];
        if (ctx->reference[i] < 1.0f)
            ctx->reference[i] = 99999.0f;
    }
    ctx->reference_len = len;
    ctx->reference_scale = true;
}

void spec_reset_reference(SpectrometerContext *ctx) {
    ctx->reference_len = 0;
    ctx->reference_scale = false;
}

float spec_x_to_nanometers(const SpectrometerContext *ctx, float x) {
    if (ctx->nm_coeff == 0.0f) return 0.0f;
    return ctx->nm_start + (x / ctx->nm_coeff);
}

float spec_x_from_nanometers(const SpectrometerContext *ctx, float nm) {
    return (nm - ctx->nm_start) * ctx->nm_coeff;
}

float spec_bin_to_x(const SpectrometerContext *ctx, int bin) {
    return (float)bin * ctx->kx;
}

int spec_x_to_bin(const SpectrometerContext *ctx, float x) {
    if (ctx->kx == 0.0f) return 0;
    return (int)(x / ctx->kx);
}

int spec_find_peaks(const SpectrometerContext *ctx, int *peaks_out,
                    int max_peaks) {
    if (max_peaks <= 0) return 0;

    int delta = (20 * ctx->src_dx) / ctx->dest_width;
    if (delta < 2) delta = 2;
    int count = 0;
    int best_idx[max_peaks];
    float best_val[max_peaks];
    int i, d;
    for (i = delta;
         i < ctx->src_dx - delta; i++) {
        float v = ctx->spec_array_filtered[i];
        if (v > ctx->spec_array_filtered[i + 1] &&
            v > ctx->spec_array_filtered[i - 1] &&
            v * 100.0f > ctx->max_value) {
            bool valid = true;
            for (d = 2; d <= delta; d++) {
                if (v < ctx->spec_array_filtered[i + d] ||
                    v < ctx->spec_array_filtered[i - d]) {
                    valid = false;
                    break;
                }
            }
            if (valid) {
                int pos = count;
                if (count < max_peaks) {
                    count++;
                } else if (pos > 0 && v <= best_val[pos - 1]) {
                    continue;
                }

                while (pos > 0 && best_val[pos - 1] < v) {
                    if (pos < max_peaks) {
                        best_val[pos] = best_val[pos - 1];
                        best_idx[pos] = best_idx[pos - 1];
                    }
                    pos--;
                }
                if (pos < max_peaks) {
                    best_val[pos] = v;
                    best_idx[pos] = i;
                }
            }
        }
    }

    for (i = 0; i < count - 1; i++) {
        for (d = i + 1; d < count; d++) {
            if (best_idx[d] < best_idx[i]) {
                int ti = best_idx[i];
                best_idx[i] = best_idx[d];
                best_idx[d] = ti;
            }
        }
    }

    for (i = 0; i < count; i++) {
        peaks_out[i] = best_idx[i];
    }

    return count;
}

int spec_collect_peaks(const SpectrometerContext *ctx, int *peaks_out,
                       float *values_out, int max_peaks) {
    if (!ctx || !peaks_out || max_peaks <= 0) return 0;

    int delta = (20 * ctx->src_dx) / ctx->dest_width;
    if (delta < 2) delta = 2;
    int count = 0;
    int i, d;
    for (i = delta;
         i < ctx->src_dx - delta && count < max_peaks; i++) {
        float v = ctx->spec_array_filtered[i];
        if (v > ctx->spec_array_filtered[i + 1] &&
            v > ctx->spec_array_filtered[i - 1] &&
            v * 100.0f > ctx->max_value) {
            bool valid = true;
            for (d = 2; d <= delta; d++) {
                if (v < ctx->spec_array_filtered[i + d] ||
                    v < ctx->spec_array_filtered[i - d]) {
                    valid = false;
                    break;
                }
            }
            if (valid) {
                peaks_out[count] = i;
                if (values_out) values_out[count] = v;
                count++;
            }
        }
    }
    return count;
}

int spec_find_dips(const SpectrometerContext *ctx, int *dips_out,
                   int max_dips) {
    int delta = (20 * ctx->src_dx) / ctx->dest_width;
    if (delta < 2) delta = 2;
    int count = 0;
    int i, d;
    for (i = delta;
         i < ctx->src_dx - delta && count < max_dips; i++) {
        float v = ctx->spec_array_filtered[i];
        if (v < ctx->spec_array_filtered[i + 1] &&
            v < ctx->spec_array_filtered[i - 1] &&
            v * 10000000.0f > ctx->max_value) {
            bool valid = true;
            for (d = 2; d <= delta; d++) {
                if (v > ctx->spec_array_filtered[i + d] ||
                    v > ctx->spec_array_filtered[i - d]) {
                    valid = false;
                    break;
                }
            }
            if (valid) {
                dips_out[count++] = i;
            }
        }
    }
    return count;
}
