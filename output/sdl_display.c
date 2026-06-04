#include "sdl_display.h"
#include "../lib/wavelength.h"
#include "../lib/colorimetry.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

#define GAMUT_LOCUS_MAX 401

typedef struct {
    double x;
    double y;
} GamutPoint;

static const GamutPoint GAMUT_SRGB[GAMUT_STAGE_COUNT] = {
    {0.6400, 0.3300},
    {0.3000, 0.6000},
    {0.1500, 0.0600}
};

static const GamutPoint GAMUT_ADOBE[GAMUT_STAGE_COUNT] = {
    {0.6400, 0.3300},
    {0.2100, 0.7100},
    {0.1500, 0.0600}
};

static const GamutPoint GAMUT_P3[GAMUT_STAGE_COUNT] = {
    {0.6800, 0.3200},
    {0.2650, 0.6900},
    {0.1500, 0.0600}
};

static const GamutPoint GAMUT_NTSC[GAMUT_STAGE_COUNT] = {
    {0.6700, 0.3300},
    {0.2100, 0.7100},
    {0.1400, 0.0800}
};

static const GamutPoint *GAMUT_REFS[GAMUT_REF_COUNT] = {
    GAMUT_SRGB,
    GAMUT_ADOBE,
    GAMUT_P3,
    GAMUT_NTSC
};

static const char *GAMUT_REF_LABELS[GAMUT_REF_COUNT] = {
    "SRGB",
    "ADOBE",
    "P3",
    "NTSC"
};

static const uint8_t GAMUT_REF_COLORS[GAMUT_REF_COUNT][3] = {
    {220, 40, 40},
    {40, 140, 40},
    {40, 80, 200},
    {180, 120, 20}
};

static const uint8_t GAMUT_STAGE_COLORS[GAMUT_SAMPLE_COUNT][3] = {
    {255, 0, 0},
    {0, 255, 0},
    {0, 0, 255},
    {255, 255, 255}
};

static const char *GAMUT_STAGE_LABELS[GAMUT_SAMPLE_COUNT] = {
    "R", "G", "B", "W"
};

static const char *GAMUT_RGB_LABELS[GAMUT_STAGE_COUNT] = {
    "R", "G", "B"
};

static void gamut_set_stage(SDLDisplay *dpy, int stage);
static void gamut_render_patch(SDLDisplay *dpy);
static void gamut_compute_metrics(SDLDisplay *dpy);
static void gamut_clear_metrics(SDLDisplay *dpy);
static void draw_gamut_diagram(SDLDisplay *dpy,
                               const SpectrometerContext *ctx,
                               uint8_t *pixels);

int sdl_display_init(SDLDisplay *dpy, int width, int spectrum_h) {
    memset(dpy, 0, sizeof(*dpy));

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }

    dpy->width = width;
    dpy->spectrum_height = spectrum_h;
    dpy->height = spectrum_h + 60;

    dpy->window = SDL_CreateWindow(
        "Spectrometer", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        dpy->width, dpy->height, SDL_WINDOW_SHOWN);
    if (!dpy->window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    dpy->renderer = SDL_CreateRenderer(
        (SDL_Window *)dpy->window, -1,
        SDL_RENDERER_ACCELERATED);
    if (!dpy->renderer) {
        SDL_DestroyWindow((SDL_Window *)dpy->window);
        SDL_Quit();
        return -1;
    }

    dpy->texture = SDL_CreateTexture(
        (SDL_Renderer *)dpy->renderer,
        SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING,
        dpy->width, dpy->height);
    if (!dpy->texture) {
        SDL_DestroyRenderer((SDL_Renderer *)dpy->renderer);
        SDL_DestroyWindow((SDL_Window *)dpy->window);
        SDL_Quit();
        return -1;
    }

    dpy->running = 1;
    dpy->key_mask = 0;
    dpy->last_frame = NULL;
    dpy->last_frame_len = 0;
    dpy->show_colorimetry = 1;
    dpy->colorimetry_valid = 0;
    dpy->cct = 0.0;
    dpy->ra = 0.0;
    dpy->last_colorimetry_ms = 0;
    dpy->gamut_mode = 0;
    dpy->gamut_window = NULL;
    dpy->gamut_renderer = NULL;
    dpy->gamut_window_id = 0;
    dpy->gamut_width = 0;
    dpy->gamut_height = 0;
    dpy->gamut_stage = 0;
    dpy->gamut_samples = 0;
    dpy->gamut_white_valid = 0;
    dpy->gamut_metrics_valid = 0;
    return 0;
}

int sdl_display_enable_gamut_mode(SDLDisplay *dpy, int width, int height) {
    if (!dpy) return -1;
    if (width <= 0) width = 320;
    if (height <= 0) height = 320;

    dpy->gamut_window = SDL_CreateWindow(
        "Gamut Patch", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        width, height, SDL_WINDOW_SHOWN);
    if (!dpy->gamut_window) {
        fprintf(stderr, "SDL_CreateWindow (gamut): %s\n", SDL_GetError());
        return -1;
    }

    dpy->gamut_renderer = SDL_CreateRenderer(
        (SDL_Window *)dpy->gamut_window, -1,
        SDL_RENDERER_ACCELERATED);
    if (!dpy->gamut_renderer) {
        fprintf(stderr, "SDL_CreateRenderer (gamut): %s\n", SDL_GetError());
        SDL_DestroyWindow((SDL_Window *)dpy->gamut_window);
        dpy->gamut_window = NULL;
        return -1;
    }

    dpy->gamut_window_id = SDL_GetWindowID((SDL_Window *)dpy->gamut_window);
    dpy->gamut_width = width;
    dpy->gamut_height = height;
    dpy->gamut_mode = 1;
    sdl_display_gamut_reset(dpy);
    return 0;
}

void sdl_display_gamut_reset(SDLDisplay *dpy) {
    if (!dpy) return;
    dpy->gamut_stage = 0;
    dpy->gamut_samples = 0;
    for (int i = 0; i < GAMUT_STAGE_COUNT; i++) {
        dpy->gamut_xy[i][0] = 0.0;
        dpy->gamut_xy[i][1] = 0.0;
    }
    dpy->gamut_white_xy[0] = 0.0;
    dpy->gamut_white_xy[1] = 0.0;
    dpy->gamut_white_valid = 0;
    gamut_clear_metrics(dpy);
    gamut_set_stage(dpy, dpy->gamut_stage);
}

int sdl_display_gamut_sample(SDLDisplay *dpy,
                             const SpectrometerContext *ctx,
                             int *out_stage,
                             double *out_x,
                             double *out_y) {
    if (!dpy || !ctx || !dpy->gamut_mode) return -1;
    if (dpy->gamut_stage >= GAMUT_SAMPLE_COUNT) return -1;

    double x = 0.0;
    double y = 0.0;
    if (colorimetry_compute_xy_from_ctx(ctx, &x, &y, NULL) < 0) {
        return -1;
    }

    int stage = dpy->gamut_stage;
    if (stage < GAMUT_STAGE_COUNT) {
        dpy->gamut_xy[stage][0] = x;
        dpy->gamut_xy[stage][1] = y;
        dpy->gamut_samples = stage + 1;
    } else if (stage == GAMUT_WHITE_STAGE) {
        dpy->gamut_white_xy[0] = x;
        dpy->gamut_white_xy[1] = y;
        dpy->gamut_white_valid = 1;
    }
    dpy->gamut_stage++;
    gamut_set_stage(dpy, dpy->gamut_stage);

    if (dpy->gamut_samples >= GAMUT_STAGE_COUNT) {
        gamut_compute_metrics(dpy);
    }

    if (out_stage) *out_stage = stage;
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    return 0;
}

static void draw_grid(SDLDisplay *dpy,
                      const SpectrometerContext *ctx,
                      uint8_t *pixels) {
    int w = dpy->width;
    int h = dpy->spectrum_height;
    int y0 = 20;

    int nm_start_div = 10 * (int)(ctx->nm_start / 10.0f);
    int nm_end_int = (int)ctx->nm_end;

    for (int i = nm_start_div; i <= nm_end_int; i += 10) {
        float xf = spec_x_from_nanometers(ctx, (float)i);
        int x = (int)(xf + 0.5f);
        if (x < 0 || x >= w) continue;

        uint8_t gray;
        if (i % 100 == 0)
            gray = 140;
        else if (i % 50 == 0)
            gray = 200;
        else
            gray = 220;

        for (int yy = y0; yy < h; yy++) {
            int off = yy * w * 3 + x * 3;
            pixels[off] = gray;
            pixels[off + 1] = gray;
            pixels[off + 2] = gray;
        }
    }
}

static void set_pixel(uint8_t *pixels, int w, int h, int x, int y,
                      uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= w || y < 0 || y >= h) return;
    int off = y * w * 3 + x * 3;
    pixels[off] = r;
    pixels[off + 1] = g;
    pixels[off + 2] = b;
}

static void draw_vline(uint8_t *pixels, int w, int h, int x,
                       int y0, int y1,
                       uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= w) return;
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (y0 < 0) y0 = 0;
    if (y1 >= h) y1 = h - 1;
    for (int y = y0; y <= y1; y++) {
        int off = y * w * 3 + x * 3;
        pixels[off] = r;
        pixels[off + 1] = g;
        pixels[off + 2] = b;
    }
}

static const uint8_t FONT_3X5_DIGITS[10][5] = {
    {0x7, 0x5, 0x5, 0x5, 0x7},
    {0x2, 0x6, 0x2, 0x2, 0x7},
    {0x7, 0x1, 0x7, 0x4, 0x7},
    {0x7, 0x1, 0x7, 0x1, 0x7},
    {0x5, 0x5, 0x7, 0x1, 0x1},
    {0x7, 0x4, 0x7, 0x1, 0x7},
    {0x7, 0x4, 0x7, 0x5, 0x7},
    {0x7, 0x1, 0x1, 0x1, 0x1},
    {0x7, 0x5, 0x7, 0x5, 0x7},
    {0x7, 0x5, 0x7, 0x1, 0x7}
};

static int glyph3x5(char c, uint8_t out[5]) {
    if (c >= '0' && c <= '9') {
        int idx = c - '0';
        if (out) {
            for (int i = 0; i < 5; i++) out[i] = FONT_3X5_DIGITS[idx][i];
        }
        return 1;
    }

    switch (c) {
        case 'A':
            if (out) { out[0] = 0x2; out[1] = 0x5; out[2] = 0x7; out[3] = 0x5; out[4] = 0x5; }
            return 1;
        case 'B':
            if (out) { out[0] = 0x7; out[1] = 0x5; out[2] = 0x7; out[3] = 0x5; out[4] = 0x7; }
            return 1;
        case 'C':
            if (out) { out[0] = 0x7; out[1] = 0x4; out[2] = 0x4; out[3] = 0x4; out[4] = 0x7; }
            return 1;
        case 'D':
            if (out) { out[0] = 0x6; out[1] = 0x5; out[2] = 0x5; out[3] = 0x5; out[4] = 0x6; }
            return 1;
        case 'E':
            if (out) { out[0] = 0x7; out[1] = 0x4; out[2] = 0x7; out[3] = 0x4; out[4] = 0x7; }
            return 1;
        case 'G':
            if (out) { out[0] = 0x7; out[1] = 0x4; out[2] = 0x5; out[3] = 0x5; out[4] = 0x7; }
            return 1;
        case 'K':
            if (out) { out[0] = 0x5; out[1] = 0x5; out[2] = 0x6; out[3] = 0x5; out[4] = 0x5; }
            return 1;
        case 'N':
            if (out) { out[0] = 0x7; out[1] = 0x5; out[2] = 0x5; out[3] = 0x5; out[4] = 0x5; }
            return 1;
        case 'O':
            if (out) { out[0] = 0x7; out[1] = 0x5; out[2] = 0x5; out[3] = 0x5; out[4] = 0x7; }
            return 1;
        case 'P':
            if (out) { out[0] = 0x7; out[1] = 0x5; out[2] = 0x7; out[3] = 0x4; out[4] = 0x4; }
            return 1;
        case 'R':
            if (out) { out[0] = 0x6; out[1] = 0x5; out[2] = 0x6; out[3] = 0x5; out[4] = 0x5; }
            return 1;
        case 'S':
            if (out) { out[0] = 0x7; out[1] = 0x4; out[2] = 0x7; out[3] = 0x1; out[4] = 0x7; }
            return 1;
        case 'T':
            if (out) { out[0] = 0x7; out[1] = 0x2; out[2] = 0x2; out[3] = 0x2; out[4] = 0x2; }
            return 1;
        case '.':
            if (out) { out[0] = 0x0; out[1] = 0x0; out[2] = 0x0; out[3] = 0x0; out[4] = 0x2; }
            return 1;
        default: return 0;
    }
}

static void draw_glyph3x5_scaled(uint8_t *pixels, int w, int h,
                                 int x, int y, int scale,
                                 char c, uint8_t r, uint8_t g, uint8_t b) {
    if (scale < 1) scale = 1;
    uint8_t rows[5];
    if (!glyph3x5(c, rows)) return;
    for (int row = 0; row < 5; row++) {
        uint8_t bits = rows[row];
        for (int col = 0; col < 3; col++) {
            if (bits & (1 << (2 - col))) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        set_pixel(pixels, w, h,
                                  x + col * scale + sx,
                                  y + row * scale + sy,
                                  r, g, b);
                    }
                }
            }
        }
    }
}

static float get_display_max(const SpectrometerContext *ctx) {
    float max_val = 0.0f;
    for (int i = 0; i < ctx->src_dx; i++) {
        if (ctx->spec_array_irradiance[i] > max_val)
            max_val = ctx->spec_array_irradiance[i];
    }

    if (max_val < 1.0f) {
        if (ctx->max_value > max_val)
            max_val = ctx->max_value;
    }
    if (max_val < 1.0f) max_val = 1.0f;
    return max_val;
}

static void draw_text3x5_scaled(uint8_t *pixels, int w, int h,
                                int x, int y, int scale,
                                const char *text,
                                uint8_t r, uint8_t g, uint8_t b) {
    if (scale < 1) scale = 1;
    if (!text) return;
    int digit_w = 3 * scale;
    int spacing = scale;
    int cursor = x;
    for (size_t i = 0; text[i] != '\0'; i++) {
        char c = (char)toupper((unsigned char)text[i]);
        if (glyph3x5(c, NULL)) {
            draw_glyph3x5_scaled(pixels, w, h, cursor, y, scale,
                                 c, r, g, b);
            cursor += digit_w + spacing;
        } else {
            cursor += spacing * 2;
        }
    }
}

static int text_width3x5_scaled(const char *text, int scale) {
    if (scale < 1) scale = 1;
    int digit_w = 3 * scale;
    int spacing = scale;
    int width = 0;
    for (size_t i = 0; text[i] != '\0'; i++) {
        char c = (char)toupper((unsigned char)text[i]);
        if (glyph3x5(c, NULL)) {
            width += digit_w + spacing;
        } else {
            width += spacing * 2;
        }
    }
    if (width > 0) width -= spacing;
    return width;
}

static void update_colorimetry(SDLDisplay *dpy,
                               const SpectrometerContext *ctx) {
    if (!dpy->show_colorimetry) return;
#if COLORIMETRY_UPDATE_MODE == COLORIMETRY_UPDATE_FIXED
    uint32_t now = SDL_GetTicks();
    if (now - dpy->last_colorimetry_ms < COLORIMETRY_FIXED_INTERVAL_MS)
        return;
    dpy->last_colorimetry_ms = now;
#else
    dpy->last_colorimetry_ms = SDL_GetTicks();
#endif

    CRIResult res;
    if (colorimetry_compute_cri_from_ctx(ctx, &res) == 0) {
        dpy->cct = res.cct;
        dpy->ra = res.ra;
        dpy->colorimetry_valid = 1;
    } else {
        dpy->colorimetry_valid = 0;
    }
}

static void draw_colorimetry(SDLDisplay *dpy,
                             const SpectrometerContext *ctx,
                             uint8_t *pixels) {
    if (!dpy->show_colorimetry) return;
    update_colorimetry(dpy, ctx);
    if (!dpy->colorimetry_valid) return;

    int scale = 4;
    int margin = 4;
    int line_h = 5 * scale + scale;
    int w = dpy->width;
    int h = dpy->height;

    int cct_int = (int)lround(dpy->cct);
    char cct_text[32];
    char ra_text[32];
    snprintf(cct_text, sizeof(cct_text), "CCT %dK", cct_int);
    snprintf(ra_text, sizeof(ra_text), "RA %.2f", dpy->ra);

    int cct_w = text_width3x5_scaled(cct_text, scale);
    int ra_w = text_width3x5_scaled(ra_text, scale);
    int text_w = cct_w > ra_w ? cct_w : ra_w;
    int x = w - margin - text_w;
    if (x < margin) x = margin;

    int y = 2;
    if (y + line_h >= h) return;
    draw_text3x5_scaled(pixels, w, h, x, y, scale,
                        cct_text, 20, 20, 20);
    y += line_h;
    if (y + 5 * scale >= h) return;
    draw_text3x5_scaled(pixels, w, h, x, y, scale,
                        ra_text, 20, 20, 20);
}

static double polygon_area_signed(const GamutPoint *pts, int count) {
    if (!pts || count < 3) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        int j = (i + 1) % count;
        sum += pts[i].x * pts[j].y - pts[j].x * pts[i].y;
    }
    return 0.5 * sum;
}

static double polygon_area_abs(const GamutPoint *pts, int count) {
    double a = polygon_area_signed(pts, count);
    return a < 0.0 ? -a : a;
}

static int point_inside_edge(GamutPoint p, GamutPoint a, GamutPoint b,
                             int ccw) {
    double cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
    return ccw ? (cross >= -1e-12) : (cross <= 1e-12);
}

static GamutPoint intersect_lines(GamutPoint p1, GamutPoint p2,
                                  GamutPoint a, GamutPoint b) {
    double A1 = p2.y - p1.y;
    double B1 = p1.x - p2.x;
    double C1 = A1 * p1.x + B1 * p1.y;

    double A2 = b.y - a.y;
    double B2 = a.x - b.x;
    double C2 = A2 * a.x + B2 * a.y;

    double det = A1 * B2 - A2 * B1;
    if (fabs(det) < 1e-12) return p2;

    GamutPoint out;
    out.x = (B2 * C1 - B1 * C2) / det;
    out.y = (A1 * C2 - A2 * C1) / det;
    return out;
}

static int clip_polygon(const GamutPoint *subject, int subj_count,
                        const GamutPoint *clip, int clip_count,
                        GamutPoint *out, int out_cap) {
    if (!subject || !clip || subj_count <= 0 || clip_count <= 0) return 0;

    GamutPoint input[16];
    GamutPoint temp[16];
    int in_count = subj_count > 16 ? 16 : subj_count;
    for (int i = 0; i < in_count; i++) input[i] = subject[i];

    int ccw = polygon_area_signed(clip, clip_count) >= 0.0;

    for (int i = 0; i < clip_count; i++) {
        GamutPoint a = clip[i];
        GamutPoint b = clip[(i + 1) % clip_count];
        int out_count = 0;
        if (in_count <= 0) break;

        for (int j = 0; j < in_count; j++) {
            GamutPoint p = input[j];
            GamutPoint q = input[(j + 1) % in_count];
            int p_in = point_inside_edge(p, a, b, ccw);
            int q_in = point_inside_edge(q, a, b, ccw);

            if (p_in && q_in) {
                if (out_count < 16) temp[out_count++] = q;
            } else if (p_in && !q_in) {
                if (out_count < 16)
                    temp[out_count++] = intersect_lines(p, q, a, b);
            } else if (!p_in && q_in) {
                if (out_count < 16)
                    temp[out_count++] = intersect_lines(p, q, a, b);
                if (out_count < 16) temp[out_count++] = q;
            }
        }

        in_count = out_count;
        for (int j = 0; j < in_count; j++) input[j] = temp[j];
    }

    int final_count = in_count > out_cap ? out_cap : in_count;
    for (int i = 0; i < final_count; i++) out[i] = input[i];
    return final_count;
}

static void gamut_clear_metrics(SDLDisplay *dpy) {
    if (!dpy) return;
    dpy->gamut_metrics_valid = 0;
    for (int i = 0; i < GAMUT_REF_COUNT; i++) {
        dpy->gamut_metrics[i].area_ratio = 0.0;
        dpy->gamut_metrics[i].coverage_ratio = 0.0;
    }
}

static void gamut_compute_metrics(SDLDisplay *dpy) {
    if (!dpy || dpy->gamut_samples < GAMUT_STAGE_COUNT) return;

    GamutPoint meas[GAMUT_STAGE_COUNT];
    for (int i = 0; i < GAMUT_STAGE_COUNT; i++) {
        meas[i].x = dpy->gamut_xy[i][0];
        meas[i].y = dpy->gamut_xy[i][1];
    }

    double area_meas = polygon_area_abs(meas, GAMUT_STAGE_COUNT);
    if (area_meas <= 0.0) {
        gamut_clear_metrics(dpy);
        return;
    }

    for (int i = 0; i < GAMUT_REF_COUNT; i++) {
        const GamutPoint *ref = GAMUT_REFS[i];
        double area_ref = polygon_area_abs(ref, GAMUT_STAGE_COUNT);
        if (area_ref <= 0.0) {
            dpy->gamut_metrics[i].area_ratio = 0.0;
            dpy->gamut_metrics[i].coverage_ratio = 0.0;
            continue;
        }
        dpy->gamut_metrics[i].area_ratio = area_meas / area_ref;

        GamutPoint inter[16];
        int inter_count = clip_polygon(meas, GAMUT_STAGE_COUNT,
                                       ref, GAMUT_STAGE_COUNT,
                                       inter, 16);
        double area_inter = polygon_area_abs(inter, inter_count);
        dpy->gamut_metrics[i].coverage_ratio = area_inter / area_ref;
    }

    dpy->gamut_metrics_valid = 1;
}

static void gamut_set_stage(SDLDisplay *dpy, int stage) {
    if (!dpy || !dpy->gamut_window) return;
    const char *label = "DONE";
    if (stage >= 0 && stage < GAMUT_SAMPLE_COUNT) {
        label = GAMUT_STAGE_LABELS[stage];
    }
    char title[64];
    snprintf(title, sizeof(title), "Gamut Patch: %s", label);
    SDL_SetWindowTitle((SDL_Window *)dpy->gamut_window, title);
}

static void gamut_render_patch(SDLDisplay *dpy) {
    if (!dpy || !dpy->gamut_window || !dpy->gamut_renderer) return;
    uint8_t r = 30, g = 30, b = 30;
    if (dpy->gamut_stage >= 0 && dpy->gamut_stage < GAMUT_SAMPLE_COUNT) {
        r = GAMUT_STAGE_COLORS[dpy->gamut_stage][0];
        g = GAMUT_STAGE_COLORS[dpy->gamut_stage][1];
        b = GAMUT_STAGE_COLORS[dpy->gamut_stage][2];
    }
    SDL_SetRenderDrawColor((SDL_Renderer *)dpy->gamut_renderer, r, g, b, 255);
    SDL_RenderClear((SDL_Renderer *)dpy->gamut_renderer);
    SDL_RenderPresent((SDL_Renderer *)dpy->gamut_renderer);
}

static void draw_line(uint8_t *pixels, int w, int h,
                      int x0, int y0, int x1, int y1,
                      uint8_t r, uint8_t g, uint8_t b) {
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    while (1) {
        set_pixel(pixels, w, h, x0, y0, r, g, b);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void draw_line_thick(uint8_t *pixels, int w, int h,
                            int x0, int y0, int x1, int y1,
                            uint8_t r, uint8_t g, uint8_t b,
                            int thickness) {
    if (thickness <= 1) {
        draw_line(pixels, w, h, x0, y0, x1, y1, r, g, b);
        return;
    }
    int half = thickness / 2;
    for (int dy = -half; dy <= half; dy++) {
        for (int dx = -half; dx <= half; dx++) {
            draw_line(pixels, w, h,
                      x0 + dx, y0 + dy,
                      x1 + dx, y1 + dy,
                      r, g, b);
        }
    }
}

static void draw_filled_triangle(uint8_t *pixels, int w, int h,
                                 int x0, int y0, int x1, int y1,
                                 int x2, int y2,
                                 uint8_t r, uint8_t g, uint8_t b) {
    int min_x = x0;
    int max_x = x0;
    int min_y = y0;
    int max_y = y0;
    if (x1 < min_x) min_x = x1;
    if (x2 < min_x) min_x = x2;
    if (x1 > max_x) max_x = x1;
    if (x2 > max_x) max_x = x2;
    if (y1 < min_y) min_y = y1;
    if (y2 < min_y) min_y = y2;
    if (y1 > max_y) max_y = y1;
    if (y2 > max_y) max_y = y2;

    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= w) max_x = w - 1;
    if (max_y >= h) max_y = h - 1;

    double ax = (double)x0, ay = (double)y0;
    double bx = (double)x1, by = (double)y1;
    double cx = (double)x2, cy = (double)y2;

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            double px = (double)x + 0.5;
            double py = (double)y + 0.5;
            double e0 = (px - ax) * (by - ay) - (py - ay) * (bx - ax);
            double e1 = (px - bx) * (cy - by) - (py - by) * (cx - bx);
            double e2 = (px - cx) * (ay - cy) - (py - cy) * (ax - cx);
            if ((e0 >= 0.0 && e1 >= 0.0 && e2 >= 0.0) ||
                (e0 <= 0.0 && e1 <= 0.0 && e2 <= 0.0)) {
                set_pixel(pixels, w, h, x, y, r, g, b);
            }
        }
    }
}

static int plot_xy_to_px(int left, int top, int right, int bottom,
                         double x_min, double x_max,
                         double y_min, double y_max,
                         double x, double y,
                         int *out_x, int *out_y) {
    if (x_max <= x_min || y_max <= y_min) return -1;
    double nx = (x - x_min) / (x_max - x_min);
    double ny = (y - y_min) / (y_max - y_min);
    if (nx < 0.0) nx = 0.0;
    if (nx > 1.0) nx = 1.0;
    if (ny < 0.0) ny = 0.0;
    if (ny > 1.0) ny = 1.0;
    if (out_x) *out_x = left + (int)lround(nx * (right - left));
    if (out_y) *out_y = bottom - (int)lround(ny * (bottom - top));
    return 0;
}

static void draw_polygon_edges(uint8_t *pixels, int w, int h,
                               int left, int top, int right, int bottom,
                               double x_min, double x_max,
                               double y_min, double y_max,
                               const GamutPoint *pts, int count,
                               uint8_t r, uint8_t g, uint8_t b,
                               int thickness) {
    if (!pts || count < 2) return;
    for (int i = 0; i < count; i++) {
        int j = (i + 1) % count;
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        plot_xy_to_px(left, top, right, bottom,
                      x_min, x_max, y_min, y_max,
                      pts[i].x, pts[i].y, &x0, &y0);
        plot_xy_to_px(left, top, right, bottom,
                      x_min, x_max, y_min, y_max,
                      pts[j].x, pts[j].y, &x1, &y1);
        draw_line_thick(pixels, w, h, x0, y0, x1, y1, r, g, b, thickness);
    }
}

static void draw_gamut_diagram(SDLDisplay *dpy,
                               const SpectrometerContext *ctx,
                               uint8_t *pixels) {
    (void)ctx;
    int w = dpy->width;
    int h = dpy->height;

    int margin = 24;
    int left = margin;
    int right = w - margin - 1;
    int top = margin;
    int bottom = h - margin - 1;
    if (right <= left || bottom <= top) return;

    double x_min = 0.0;
    double x_max = 0.8;
    double y_min = 0.0;
    double y_max = 0.9;

    uint8_t grid = 210;
    int grid_thickness = 2;
    int axis_thickness = 3;
    int locus_thickness = 2;
    int ref_thickness = 3;
    int meas_thickness = 3;

    for (int xi = 0; xi <= 8; xi++) {
        double x = x_min + 0.1 * xi;
        int px = 0;
        plot_xy_to_px(left, top, right, bottom,
                      x_min, x_max, y_min, y_max,
                      x, y_min, &px, NULL);
        draw_line_thick(pixels, w, h, px, top, px, bottom,
                        grid, grid, grid, grid_thickness);
    }
    for (int yi = 0; yi <= 9; yi++) {
        double y = y_min + 0.1 * yi;
        int py = 0;
        plot_xy_to_px(left, top, right, bottom,
                      x_min, x_max, y_min, y_max,
                      x_min, y, NULL, &py);
        draw_line_thick(pixels, w, h, left, py, right, py,
                        grid, grid, grid, grid_thickness);
    }

    draw_line_thick(pixels, w, h, left, bottom, right, bottom,
                    120, 120, 120, axis_thickness);
    draw_line_thick(pixels, w, h, left, top, left, bottom,
                    120, 120, 120, axis_thickness);

    static int locus_loaded = 0;
    static int locus_count = 0;
    static double locus_x[GAMUT_LOCUS_MAX];
    static double locus_y[GAMUT_LOCUS_MAX];
    if (!locus_loaded) {
        if (colorimetry_get_spectral_locus(locus_x, locus_y,
                                           GAMUT_LOCUS_MAX,
                                           &locus_count) == 0) {
            locus_loaded = 1;
        }
    }
    if (locus_loaded && locus_count > 1) {
        for (int i = 0; i < locus_count - 1; i++) {
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            plot_xy_to_px(left, top, right, bottom,
                          x_min, x_max, y_min, y_max,
                          locus_x[i], locus_y[i], &x0, &y0);
            plot_xy_to_px(left, top, right, bottom,
                          x_min, x_max, y_min, y_max,
                          locus_x[i + 1], locus_y[i + 1], &x1, &y1);
            draw_line_thick(pixels, w, h, x0, y0, x1, y1,
                            80, 80, 80, locus_thickness);
        }
    }

    for (int i = 0; i < GAMUT_REF_COUNT; i++) {
        const GamutPoint *ref = GAMUT_REFS[i];
        draw_polygon_edges(pixels, w, h, left, top, right, bottom,
                           x_min, x_max, y_min, y_max,
                           ref, GAMUT_STAGE_COUNT,
                           GAMUT_REF_COLORS[i][0],
                           GAMUT_REF_COLORS[i][1],
                           GAMUT_REF_COLORS[i][2],
                           ref_thickness);
    }

    if (dpy->gamut_samples > 0) {
        GamutPoint meas[GAMUT_STAGE_COUNT];
        int count = dpy->gamut_samples;
        if (count > GAMUT_STAGE_COUNT) count = GAMUT_STAGE_COUNT;
        for (int i = 0; i < count; i++) {
            meas[i].x = dpy->gamut_xy[i][0];
            meas[i].y = dpy->gamut_xy[i][1];
        }
        if (count == GAMUT_STAGE_COUNT) {
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            plot_xy_to_px(left, top, right, bottom,
                          x_min, x_max, y_min, y_max,
                          meas[0].x, meas[0].y, &x0, &y0);
            plot_xy_to_px(left, top, right, bottom,
                          x_min, x_max, y_min, y_max,
                          meas[1].x, meas[1].y, &x1, &y1);
            plot_xy_to_px(left, top, right, bottom,
                          x_min, x_max, y_min, y_max,
                          meas[2].x, meas[2].y, &x2, &y2);
            draw_filled_triangle(pixels, w, h,
                                 x0, y0, x1, y1, x2, y2,
                                 210, 210, 210);
        }
        if (count >= 2) {
            draw_polygon_edges(pixels, w, h, left, top, right, bottom,
                               x_min, x_max, y_min, y_max,
                               meas, count, 20, 20, 20, meas_thickness);
        }
        for (int i = 0; i < count; i++) {
            int px = 0, py = 0;
            plot_xy_to_px(left, top, right, bottom,
                          x_min, x_max, y_min, y_max,
                          meas[i].x, meas[i].y, &px, &py);
            set_pixel(pixels, w, h, px, py,
                      GAMUT_STAGE_COLORS[i][0],
                      GAMUT_STAGE_COLORS[i][1],
                      GAMUT_STAGE_COLORS[i][2]);
            draw_text3x5_scaled(pixels, w, h, px + 6, py - 9, 3,
                                GAMUT_RGB_LABELS[i],
                                10, 10, 10);
        }
    }

    if (dpy->gamut_white_valid) {
        int px = 0, py = 0;
        plot_xy_to_px(left, top, right, bottom,
                      x_min, x_max, y_min, y_max,
                      dpy->gamut_white_xy[0], dpy->gamut_white_xy[1],
                      &px, &py);
        draw_line_thick(pixels, w, h, px - 3, py, px + 3, py,
                        0, 0, 0, 2);
        draw_line_thick(pixels, w, h, px, py - 3, px, py + 3,
                        0, 0, 0, 2);
        draw_text3x5_scaled(pixels, w, h, px + 6, py - 9, 3,
                            "W", 10, 10, 10);
    }

    if (dpy->gamut_metrics_valid) {
        int scale = 3;
        int line_h = 5 * scale + scale;
        int x = left + 4;
        int y = top + 4;
        for (int i = 0; i < GAMUT_REF_COUNT; i++) {
            int area_pct = (int)lround(dpy->gamut_metrics[i].area_ratio * 100.0);
            int cov_pct = (int)lround(dpy->gamut_metrics[i].coverage_ratio * 100.0);
            if (area_pct < 0) area_pct = 0;
            if (cov_pct < 0) cov_pct = 0;
            if (area_pct > 999) area_pct = 999;
            if (cov_pct > 999) cov_pct = 999;
            char line[32];
            snprintf(line, sizeof(line), "%s A%d C%d",
                     GAMUT_REF_LABELS[i], area_pct, cov_pct);
            draw_text3x5_scaled(pixels, w, h, x, y, scale,
                                line,
                                GAMUT_REF_COLORS[i][0],
                                GAMUT_REF_COLORS[i][1],
                                GAMUT_REF_COLORS[i][2]);
            y += line_h;
        }
    }
}

static void draw_spectrum(SDLDisplay *dpy,
                          const SpectrometerContext *ctx,
                          uint8_t *pixels) {
    int w = dpy->width;
    int h = dpy->spectrum_height;
    float ky = (float)(h - 20 - 15) / get_display_max(ctx);

    float old_x = -99.0f;
    float old_y = 0.0f;

    for (int i = 0; i < ctx->src_dx; i++) {
        float x = spec_bin_to_x(ctx, (float)i);
        float val = ctx->spec_array_irradiance[i];
        float y = (float)(h - 20) - val * ky;

        int xi = (int)(x + 0.5f);
        int yi = (int)(y + 0.5f);
        if (xi >= 0 && xi < w && yi >= 0 && yi < h) {
            int off = yi * w * 3 + xi * 3;
            pixels[off] = 0;
            pixels[off + 1] = 140;
            pixels[off + 2] = 0;
        }

        if (old_x >= 0 && xi >= 0 && xi < w) {
            int x0 = (int)(old_x + 0.5f);
            int x1 = xi;
            if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
            float y0f = old_y;
            float y1f = y;
            for (int xx = x0; xx <= x1 && xx < w; xx++) {
                float t = (xx - x0) / (float)(x1 - x0 + 1);
                int yy = (int)(y0f + (y1f - y0f) * t + 0.5f);
                if (yy >= 0 && yy < h) {
                    int off = yy * w * 3 + xx * 3;
                    pixels[off] = 0;
                    pixels[off + 1] = 140;
                    pixels[off + 2] = 0;
                }
            }
        }
        old_x = x;
        old_y = y;
    }
}

static void draw_color_bars(SDLDisplay *dpy,
                            const SpectrometerContext *ctx,
                            uint8_t *pixels) {
    int w = dpy->width;
    int h = dpy->spectrum_height;
    float ky = (float)(h - 20 - 15) / get_display_max(ctx);

    for (int i = 0; i < ctx->src_dx; i++) {
        float x = spec_bin_to_x(ctx, (float)i);
        int xi = (int)(x + 0.5f);
        if (xi < 0 || xi >= w) continue;

        float nm = spec_x_to_nanometers(ctx, x);
        ColorRGB color = wavelength_to_color(nm);
        float val = ctx->spec_array_irradiance[i];
        int yh = (int)(val * ky + 0.5f);

        for (int yy = h - 20; yy > h - 20 - yh && yy >= 0; yy--) {
            int off = yy * w * 3 + xi * 3;
            pixels[off] = color.r;
            pixels[off + 1] = color.g;
            pixels[off + 2] = color.b;
        }
    }
}

static void draw_peaks(SDLDisplay *dpy,
                       const SpectrometerContext *ctx,
                       uint8_t *pixels) {
    if (!ctx->show_peaks) return;

    int main_peaks[8];
    int main_count = spec_find_peaks(ctx, main_peaks, MAX_PEAKS);
    int all_peaks[64];
    float all_vals[64];
    int all_count = spec_collect_peaks(ctx, all_peaks, all_vals, 64);
    if (main_count <= 0 && all_count <= 0) return;

    int w = dpy->width;
    int h = dpy->height;
    int spectrum_h = dpy->spectrum_height;
    int y0 = 20;
    int y1 = spectrum_h - 20;
    if (y1 < y0) y1 = spectrum_h - 1;

    for (int i = 0; i < main_count; i++) {
        float x = spec_bin_to_x(ctx, main_peaks[i]);
        int xi = (int)(x + 0.5f);
        draw_vline(pixels, w, h, xi, y0, y1, 255, 0, 0);

        float nm = spec_x_to_nanometers(ctx, x);
        int nm_int = (int)(nm + 0.5f);
        char label[8];
        snprintf(label, sizeof(label), "%d", nm_int);
        int scale = 3;
        int digit_w = 3 * scale;
        int spacing = scale;
        int text_len = (int)strlen(label);
        int text_w = text_len > 0 ? text_len * (digit_w + spacing) - spacing : 0;
        int tx = xi - text_w / 2;
        if (tx < 0) tx = 0;
        if (text_w >= w) {
            tx = 0;
        } else if (tx + text_w >= w) {
            tx = w - text_w - 1;
        }
        draw_text3x5_scaled(pixels, w, h, tx, 2, scale,
                            label, 255, 0, 0);
    }

    if (all_count <= 0 || ctx->max_value <= 0.0f) return;

    float aux_min = ctx->max_value * PEAK_RATIO;
    int aux_y = dpy->spectrum_height + 8;
    if (aux_y + 6 >= h) aux_y = h - 6;

    int last_end = -10000;
    for (int i = 0; i < all_count; i++) {
        bool is_main = false;
        for (int j = 0; j < main_count; j++) {
            if (all_peaks[i] == main_peaks[j]) {
                is_main = true;
                break;
            }
        }
        if (is_main) continue;
        if (all_vals[i] < aux_min) continue;

        float x = spec_bin_to_x(ctx, all_peaks[i]);
        int xi = (int)(x + 0.5f);
        draw_vline(pixels, w, h, xi, y0, y1, 200, 0, 0);
        float nm = spec_x_to_nanometers(ctx, x);
        int nm_int = (int)(nm + 0.5f);
        char label[8];
        snprintf(label, sizeof(label), "%d", nm_int);

        int scale = 2;
        int digit_w = 3 * scale;
        int spacing = scale;
        int text_len = (int)strlen(label);
        int text_w = text_len > 0 ? text_len * (digit_w + spacing) - spacing : 0;
        int tx = xi - text_w / 2;
        if (tx < 0) tx = 0;
        if (text_w >= w) {
            tx = 0;
        } else if (tx + text_w >= w) {
            tx = w - text_w - 1;
        }
        if (tx <= last_end + 2) {
            int shifted = last_end + 2;
            if (shifted + text_w >= w) continue;
            tx = shifted;
        }

        draw_text3x5_scaled(pixels, w, h, tx, aux_y, scale,
                            label, 200, 0, 0);
        last_end = tx + text_w;
    }
}

void sdl_display_render(SDLDisplay *dpy,
                        const SpectrometerContext *ctx) {
    SDL_Event e;
    dpy->key_mask = 0;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT)
            dpy->running = 0;
        if (e.type == SDL_WINDOWEVENT &&
            e.window.event == SDL_WINDOWEVENT_CLOSE) {
            if (dpy->gamut_window &&
                e.window.windowID == dpy->gamut_window_id) {
                if (dpy->gamut_renderer) {
                    SDL_DestroyRenderer((SDL_Renderer *)dpy->gamut_renderer);
                }
                if (dpy->gamut_window) {
                    SDL_DestroyWindow((SDL_Window *)dpy->gamut_window);
                }
                dpy->gamut_renderer = NULL;
                dpy->gamut_window = NULL;
                dpy->gamut_window_id = 0;
                dpy->gamut_mode = 0;
                dpy->running = 0;
            }
        }
        if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_ESCAPE)
                dpy->running = 0;
            if (e.key.keysym.sym == SDLK_s)
                dpy->key_mask |= SDL_KEYMASK_SAVE;
            if (e.key.keysym.sym == SDLK_v)
                dpy->key_mask |= SDL_KEYMASK_CALIBRATE;
            if (e.key.keysym.sym == SDLK_c)
                dpy->key_mask |= SDL_KEYMASK_COLORIMETRY;
            if (e.key.keysym.sym == SDLK_SPACE)
                dpy->key_mask |= SDL_KEYMASK_PAUSE;
            if (e.key.keysym.sym == SDLK_RETURN ||
                e.key.keysym.sym == SDLK_KP_ENTER)
                dpy->key_mask |= SDL_KEYMASK_GAMUT_SAMPLE;
            if (e.key.keysym.sym == SDLK_BACKSPACE)
                dpy->key_mask |= SDL_KEYMASK_GAMUT_RESET;
            if (e.key.keysym.sym == SDLK_LEFTBRACKET)
                dpy->key_mask |= SDL_KEYMASK_EXPOSURE_DEC;
            if (e.key.keysym.sym == SDLK_RIGHTBRACKET)
                dpy->key_mask |= SDL_KEYMASK_EXPOSURE_INC;
        }
    }
    if (!dpy->running) return;

    uint8_t *pixels;
    int pitch;
    SDL_LockTexture((SDL_Texture *)dpy->texture, NULL,
                    (void **)&pixels, &pitch);

    memset(pixels, 240, (size_t)dpy->height * pitch);

    if (dpy->gamut_mode) {
        draw_gamut_diagram(dpy, ctx, pixels);
    } else {
        draw_grid(dpy, ctx, pixels);
        if (ctx->reference_scale) {
            draw_spectrum(dpy, ctx, pixels);
        }
        draw_color_bars(dpy, ctx, pixels);
        draw_spectrum(dpy, ctx, pixels);
        draw_peaks(dpy, ctx, pixels);
        draw_colorimetry(dpy, ctx, pixels);
    }

    int row_bytes = dpy->width * 3;
    int needed = row_bytes * dpy->height;
    if (dpy->last_frame_len != needed) {
        free(dpy->last_frame);
        dpy->last_frame = (uint8_t *)malloc((size_t)needed);
        dpy->last_frame_len = dpy->last_frame ? needed : 0;
    }
    if (dpy->last_frame) {
        for (int y = 0; y < dpy->height; y++) {
            memcpy(dpy->last_frame + y * row_bytes,
                   pixels + y * pitch,
                   (size_t)row_bytes);
        }
    }

    SDL_UnlockTexture((SDL_Texture *)dpy->texture);

    SDL_RenderCopy((SDL_Renderer *)dpy->renderer,
                   (SDL_Texture *)dpy->texture, NULL, NULL);
    SDL_RenderPresent((SDL_Renderer *)dpy->renderer);
    if (dpy->gamut_mode) {
        gamut_render_patch(dpy);
    }
}

int sdl_display_save_ppm(SDLDisplay *dpy, const char *path) {
    if (!dpy || !path || !dpy->last_frame) return -1;
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;

    fprintf(fp, "P6\n%d %d\n255\n", dpy->width, dpy->height);
    size_t bytes = (size_t)dpy->width * dpy->height * 3;
    if (fwrite(dpy->last_frame, 1, bytes, fp) != bytes) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

void sdl_display_close(SDLDisplay *dpy) {
    if (dpy->last_frame) {
        free(dpy->last_frame);
    }
    if (dpy->texture)
        SDL_DestroyTexture((SDL_Texture *)dpy->texture);
    if (dpy->renderer)
        SDL_DestroyRenderer((SDL_Renderer *)dpy->renderer);
    if (dpy->window)
        SDL_DestroyWindow((SDL_Window *)dpy->window);
    if (dpy->gamut_renderer)
        SDL_DestroyRenderer((SDL_Renderer *)dpy->gamut_renderer);
    if (dpy->gamut_window)
        SDL_DestroyWindow((SDL_Window *)dpy->gamut_window);
    SDL_Quit();
    memset(dpy, 0, sizeof(*dpy));
}
