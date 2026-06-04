#include "icc.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define ICC_TAG_MAX 16

typedef struct {
    uint32_t sig;
    uint8_t *data;
    uint32_t size;
} IccTag;

static void write_u32_be(uint8_t *dst, uint32_t v) {
    dst[0] = (uint8_t)((v >> 24) & 0xff);
    dst[1] = (uint8_t)((v >> 16) & 0xff);
    dst[2] = (uint8_t)((v >> 8) & 0xff);
    dst[3] = (uint8_t)(v & 0xff);
}

static void write_u16_be(uint8_t *dst, uint16_t v) {
    dst[0] = (uint8_t)((v >> 8) & 0xff);
    dst[1] = (uint8_t)(v & 0xff);
}

static uint32_t make_sig(const char *s) {
    return ((uint32_t)s[0] << 24) |
           ((uint32_t)s[1] << 16) |
           ((uint32_t)s[2] << 8) |
           (uint32_t)s[3];
}

static int s15fixed16(double v) {
    double scaled = v * 65536.0;
    if (scaled > 2147483647.0) scaled = 2147483647.0;
    if (scaled < -2147483648.0) scaled = -2147483648.0;
    return (int)lround(scaled);
}

static void xy_to_xyz(double x, double y, double out[3]) {
    if (y <= 0.0) {
        out[0] = out[1] = out[2] = 0.0;
        return;
    }
    out[0] = x / y;
    out[1] = 1.0;
    out[2] = (1.0 - x - y) / y;
}

static int invert3x3(const double m[3][3], double inv_out[3][3]) {
    double a = m[0][0], b = m[0][1], c = m[0][2];
    double d = m[1][0], e = m[1][1], f = m[1][2];
    double g = m[2][0], h = m[2][1], i = m[2][2];

    double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (fabs(det) < 1e-12) return -1;
    double inv_det = 1.0 / det;

    inv_out[0][0] = (e * i - f * h) * inv_det;
    inv_out[0][1] = (c * h - b * i) * inv_det;
    inv_out[0][2] = (b * f - c * e) * inv_det;
    inv_out[1][0] = (f * g - d * i) * inv_det;
    inv_out[1][1] = (a * i - c * g) * inv_det;
    inv_out[1][2] = (c * d - a * f) * inv_det;
    inv_out[2][0] = (d * h - e * g) * inv_det;
    inv_out[2][1] = (b * g - a * h) * inv_det;
    inv_out[2][2] = (a * e - b * d) * inv_det;
    return 0;
}

static void mat3_mul_vec(const double m[3][3], const double v[3], double out[3]) {
    out[0] = m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2];
    out[1] = m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2];
    out[2] = m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2];
}

static void mat3_mul(const double a[3][3], const double b[3][3], double out[3][3]) {
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            out[r][c] = a[r][0] * b[0][c]
                      + a[r][1] * b[1][c]
                      + a[r][2] * b[2][c];
        }
    }
}

static void bradford_adapt_matrix(const double src_w[3], const double dst_w[3],
                                  double out[3][3]) {
    const double M[3][3] = {
        {0.8951, 0.2664, -0.1614},
        {-0.7502, 1.7135, 0.0367},
        {0.0389, -0.0685, 1.0296}
    };
    const double M_INV[3][3] = {
        {0.9869929, -0.1470543, 0.1599627},
        {0.4323053, 0.5183603, 0.0492912},
        {-0.0085287, 0.0400428, 0.9684867}
    };

    double src_lms[3];
    double dst_lms[3];
    mat3_mul_vec(M, src_w, src_lms);
    mat3_mul_vec(M, dst_w, dst_lms);

    double D[3][3] = {
        {dst_lms[0] / src_lms[0], 0.0, 0.0},
        {0.0, dst_lms[1] / src_lms[1], 0.0},
        {0.0, 0.0, dst_lms[2] / src_lms[2]}
    };

    double temp[3][3];
    mat3_mul(D, M, temp);
    mat3_mul(M_INV, temp, out);
}

static int build_rgb_to_xyz(double rx, double ry,
                            double gx, double gy,
                            double bx, double by,
                            double wx, double wy,
                            double out_r[3],
                            double out_g[3],
                            double out_b[3]) {
    double R[3], G[3], B[3], W[3];
    xy_to_xyz(rx, ry, R);
    xy_to_xyz(gx, gy, G);
    xy_to_xyz(bx, by, B);
    xy_to_xyz(wx, wy, W);

    double M[3][3] = {
        {R[0], G[0], B[0]},
        {R[1], G[1], B[1]},
        {R[2], G[2], B[2]}
    };
    double M_INV[3][3];
    if (invert3x3(M, M_INV) < 0) return -1;

    double S[3];
    mat3_mul_vec(M_INV, W, S);

    out_r[0] = R[0] * S[0];
    out_r[1] = R[1] * S[0];
    out_r[2] = R[2] * S[0];
    out_g[0] = G[0] * S[1];
    out_g[1] = G[1] * S[1];
    out_g[2] = G[2] * S[1];
    out_b[0] = B[0] * S[2];
    out_b[1] = B[1] * S[2];
    out_b[2] = B[2] * S[2];
    return 0;
}

static uint8_t *make_xyz_tag(const double xyz[3], uint32_t *out_size) {
    uint32_t size = 20;
    uint8_t *buf = (uint8_t *)calloc(1, size);
    if (!buf) return NULL;
    write_u32_be(buf, make_sig("XYZ "));
    int x = s15fixed16(xyz[0]);
    int y = s15fixed16(xyz[1]);
    int z = s15fixed16(xyz[2]);
    write_u32_be(buf + 8, (uint32_t)x);
    write_u32_be(buf + 12, (uint32_t)y);
    write_u32_be(buf + 16, (uint32_t)z);
    if (out_size) *out_size = size;
    return buf;
}

static uint8_t *make_curv_gamma(double gamma, uint32_t *out_size) {
    uint32_t size = 12 + 2;
    uint8_t *buf = (uint8_t *)calloc(1, size);
    if (!buf) return NULL;
    write_u32_be(buf, make_sig("curv"));
    write_u32_be(buf + 8, 1);
    double g = gamma;
    if (g < 0.1) g = 0.1;
    if (g > 10.0) g = 10.0;
    uint16_t gfix = (uint16_t)lround(g * 256.0);
    write_u16_be(buf + 12, gfix);
    if (out_size) *out_size = size;
    return buf;
}

static void free_tags(IccTag *tags, int count) {
    for (int i = 0; i < count; i++) {
        free(tags[i].data);
        tags[i].data = NULL;
        tags[i].size = 0;
    }
}

int icc_write_display_profile(const char *path,
                              double rx, double ry,
                              double gx, double gy,
                              double bx, double by,
                              double wx, double wy,
                              double gamma,
                              const char *description) {
    if (!path) return -1;
    (void)description;
    if (wy <= 0.0) return -1;

    double r_xyz[3];
    double g_xyz[3];
    double b_xyz[3];
    if (build_rgb_to_xyz(rx, ry, gx, gy, bx, by, wx, wy,
                         r_xyz, g_xyz, b_xyz) < 0) {
        return -1;
    }

    const double D50[3] = {0.9642, 1.0, 0.8251};
    double src_w[3];
    xy_to_xyz(wx, wy, src_w);

    double adapt[3][3];
    bradford_adapt_matrix(src_w, D50, adapt);

    double r_d50[3];
    double g_d50[3];
    double b_d50[3];
    mat3_mul_vec(adapt, r_xyz, r_d50);
    mat3_mul_vec(adapt, g_xyz, g_d50);
    mat3_mul_vec(adapt, b_xyz, b_d50);

    IccTag tags[ICC_TAG_MAX];
    int tag_count = 0;
    memset(tags, 0, sizeof(tags));

    uint32_t size = 0;
    uint8_t *wtpt = make_xyz_tag(D50, &size);
    if (!wtpt) return -1;
    tags[tag_count++] = (IccTag){ make_sig("wtpt"), wtpt, size };

    uint8_t *rxyz = make_xyz_tag(r_d50, &size);
    uint8_t *gxyz = make_xyz_tag(g_d50, &size);
    uint8_t *bxyz = make_xyz_tag(b_d50, &size);
    if (!rxyz || !gxyz || !bxyz) {
        free(wtpt);
        free(rxyz);
        free(gxyz);
        free(bxyz);
        return -1;
    }
    tags[tag_count++] = (IccTag){ make_sig("rXYZ"), rxyz, size };
    tags[tag_count++] = (IccTag){ make_sig("gXYZ"), gxyz, size };
    tags[tag_count++] = (IccTag){ make_sig("bXYZ"), bxyz, size };

    uint8_t *rtrc = make_curv_gamma(gamma, &size);
    uint8_t *gtrc = make_curv_gamma(gamma, &size);
    uint8_t *btrc = make_curv_gamma(gamma, &size);
    if (!rtrc || !gtrc || !btrc) {
        free_tags(tags, tag_count);
        free(rtrc);
        free(gtrc);
        free(btrc);
        return -1;
    }
    tags[tag_count++] = (IccTag){ make_sig("rTRC"), rtrc, size };
    tags[tag_count++] = (IccTag){ make_sig("gTRC"), gtrc, size };
    tags[tag_count++] = (IccTag){ make_sig("bTRC"), btrc, size };

    uint32_t tag_table_size = 4 + tag_count * 12;
    uint32_t offset = 128 + tag_table_size;
    for (int i = 0; i < tag_count; i++) {
        uint32_t pad = (4 - (offset % 4)) % 4;
        offset += pad;
        uint32_t aligned = (tags[i].size + 3) & ~3u;
        offset += aligned;
    }
    uint32_t profile_size = offset;

    uint8_t *buf = (uint8_t *)calloc(1, profile_size);
    if (!buf) {
        free_tags(tags, tag_count);
        return -1;
    }

    write_u32_be(buf + 0, profile_size);
    write_u32_be(buf + 8, 0x02100000);
    write_u32_be(buf + 12, make_sig("mntr"));
    write_u32_be(buf + 16, make_sig("RGB "));
    write_u32_be(buf + 20, make_sig("XYZ "));

    time_t now = time(NULL);
    struct tm tm_now;
    gmtime_r(&now, &tm_now);
    write_u16_be(buf + 24, (uint16_t)(tm_now.tm_year + 1900));
    write_u16_be(buf + 26, (uint16_t)(tm_now.tm_mon + 1));
    write_u16_be(buf + 28, (uint16_t)tm_now.tm_mday);
    write_u16_be(buf + 30, (uint16_t)tm_now.tm_hour);
    write_u16_be(buf + 32, (uint16_t)tm_now.tm_min);
    write_u16_be(buf + 34, (uint16_t)tm_now.tm_sec);

    write_u32_be(buf + 36, make_sig("acsp"));
    write_u32_be(buf + 64, 0);

    int X = s15fixed16(D50[0]);
    int Y = s15fixed16(D50[1]);
    int Z = s15fixed16(D50[2]);
    write_u32_be(buf + 68, (uint32_t)X);
    write_u32_be(buf + 72, (uint32_t)Y);
    write_u32_be(buf + 76, (uint32_t)Z);

    write_u32_be(buf + 128, (uint32_t)tag_count);

    uint32_t tag_offset = 128 + tag_table_size;
    for (int i = 0; i < tag_count; i++) {
        uint32_t pad = (4 - (tag_offset % 4)) % 4;
        tag_offset += pad;
        uint32_t tag_pos = 132 + i * 12;
        uint32_t aligned = (tags[i].size + 3) & ~3u;
        write_u32_be(buf + tag_pos, tags[i].sig);
        write_u32_be(buf + tag_pos + 4, tag_offset);
        write_u32_be(buf + tag_pos + 8, tags[i].size);

        memcpy(buf + tag_offset, tags[i].data, tags[i].size);
        tag_offset += aligned;
    }

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        free(buf);
        free_tags(tags, tag_count);
        return -1;
    }
    if (fwrite(buf, 1, profile_size, fp) != profile_size) {
        fclose(fp);
        free(buf);
        free_tags(tags, tag_count);
        return -1;
    }
    fclose(fp);
    free(buf);
    free_tags(tags, tag_count);
    return 0;
}
