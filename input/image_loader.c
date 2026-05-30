#include "image_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_ppm(const char *filename, ImageData *img) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return -1;

    char magic[3];
    if (fscanf(fp, "%2s", magic) != 1) { fclose(fp); return -1; }
    magic[2] = '\0';

    if (strcmp(magic, "P6") != 0) { fclose(fp); return -1; }

    int w, h, maxval;
    if (fscanf(fp, "%d %d", &w, &h) != 2) { fclose(fp); return -1; }
    if (fscanf(fp, "%d", &maxval) != 1) { fclose(fp); return -1; }
    fgetc(fp);

    if (w <= 0 || h <= 0 || maxval <= 0 || maxval > 255) {
        fclose(fp);
        return -1;
    }

    img->data = (uint8_t *)malloc((size_t)w * h * 3);
    if (!img->data) { fclose(fp); return -1; }

    size_t read_count = fread(img->data, 1, (size_t)w * h * 3, fp);
    fclose(fp);

    if (read_count != (size_t)w * h * 3) {
        free(img->data);
        img->data = NULL;
        return -1;
    }

    img->width = w;
    img->height = h;
    img->channels = 3;
    return 0;
}

int image_load(const char *filename, ImageData *img) {
    memset(img, 0, sizeof(*img));

    if (read_ppm(filename, img) == 0)
        return 0;

    return -1;
}

void image_free(ImageData *img) {
    if (img->data) {
        free(img->data);
        img->data = NULL;
    }
}
