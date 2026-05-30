#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H

#include <stdint.h>

typedef struct {
    uint8_t *data;
    int width;
    int height;
    int channels;
} ImageData;

int  image_load(const char *filename, ImageData *img);
void image_free(ImageData *img);

#endif
