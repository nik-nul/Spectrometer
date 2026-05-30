#ifndef V4L2_H
#define V4L2_H

#include <stdint.h>
#include <stddef.h>

#define V4L2_BUFFER_COUNT 4

typedef struct {
    int fd;
    int width;
    int height;
    int stride;
    uint32_t pixel_format;
    uint8_t *buffer;
    int buffer_len;
    uint8_t *rgb_buffer;
    int rgb_buffer_len;
    void *buffers[V4L2_BUFFER_COUNT];
    size_t buffer_sizes[V4L2_BUFFER_COUNT];
    int buffer_count;
} V4L2Device;

int  v4l2_open(V4L2Device *dev, const char *device_path,
               int width, int height);
int  v4l2_capture_frame(V4L2Device *dev);
int  v4l2_get_exposure(V4L2Device *dev, int *exposure_out);
int  v4l2_set_exposure(V4L2Device *dev, int exposure);
void v4l2_close(V4L2Device *dev);

#endif
