#include "sdl_display.h"
#include "../lib/wavelength.h"
#include "../lib/colorimetry.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

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
        case 'C':
            if (out) { out[0] = 0x7; out[1] = 0x4; out[2] = 0x4; out[3] = 0x4; out[4] = 0x7; }
            return 1;
        case 'K':
            if (out) { out[0] = 0x5; out[1] = 0x5; out[2] = 0x6; out[3] = 0x5; out[4] = 0x5; }
            return 1;
        case 'R':
            if (out) { out[0] = 0x6; out[1] = 0x5; out[2] = 0x6; out[3] = 0x5; out[4] = 0x5; }
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
        if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_ESCAPE)
                dpy->running = 0;
            if (e.key.keysym.sym == SDLK_s)
                dpy->key_mask |= SDL_KEYMASK_SAVE;
            if (e.key.keysym.sym == SDLK_v)
                dpy->key_mask |= SDL_KEYMASK_CALIBRATE;
            if (e.key.keysym.sym == SDLK_c)
                dpy->key_mask |= SDL_KEYMASK_COLORIMETRY;
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

    draw_grid(dpy, ctx, pixels);
    if (ctx->reference_scale) {
        draw_spectrum(dpy, ctx, pixels);
    }
    draw_color_bars(dpy, ctx, pixels);
    draw_spectrum(dpy, ctx, pixels);
    draw_peaks(dpy, ctx, pixels);
    draw_colorimetry(dpy, ctx, pixels);

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
    SDL_Quit();
    memset(dpy, 0, sizeof(*dpy));
}
