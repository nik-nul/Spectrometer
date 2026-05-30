#include "v4l2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <jpeglib.h>
#include <setjmp.h>

static uint8_t clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

typedef struct {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
} JpegErrorMgr;

static void jpeg_error_exit(j_common_ptr cinfo) {
    JpegErrorMgr *err = (JpegErrorMgr *)cinfo->err;
    longjmp(err->setjmp_buffer, 1);
}

static int mjpeg_to_rgb24(const uint8_t *src, size_t len,
                          uint8_t *dst, int width, int height) {
    struct jpeg_decompress_struct cinfo;
    JpegErrorMgr jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_error_exit;
    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, src, len);
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    if ((int)cinfo.output_width != width ||
        (int)cinfo.output_height != height ||
        cinfo.output_components != 3) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    int row_bytes = width * 3;
    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row_ptr = dst + cinfo.output_scanline * row_bytes;
        jpeg_read_scanlines(&cinfo, &row_ptr, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return 0;
}

static void copy_rgb24_packed(const uint8_t *src, uint8_t *dst,
                              int width, int height, int stride) {
    int row_bytes = width * 3;
    for (int y = 0; y < height; y++) {
        const uint8_t *row = src + y * stride;
        uint8_t *out = dst + y * row_bytes;
        memcpy(out, row, (size_t)row_bytes);
    }
}

static void yuyv_to_rgb24(const uint8_t *src, uint8_t *dst,
                          int width, int height, int stride) {
    int out_row_bytes = width * 3;
    for (int y = 0; y < height; y++) {
        const uint8_t *row = src + y * stride;
        uint8_t *out = dst + y * out_row_bytes;
        for (int x = 0; x + 1 < width; x += 2) {
            int y0 = row[0];
            int u  = row[1];
            int y1 = row[2];
            int v  = row[3];

            int c = y0 - 16;
            int d = u - 128;
            int e = v - 128;
            int r = (298 * c + 409 * e + 128) >> 8;
            int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
            int b = (298 * c + 516 * d + 128) >> 8;
            out[0] = clamp_u8(r);
            out[1] = clamp_u8(g);
            out[2] = clamp_u8(b);

            c = y1 - 16;
            r = (298 * c + 409 * e + 128) >> 8;
            g = (298 * c - 100 * d - 208 * e + 128) >> 8;
            b = (298 * c + 516 * d + 128) >> 8;
            out[3] = clamp_u8(r);
            out[4] = clamp_u8(g);
            out[5] = clamp_u8(b);

            row += 4;
            out += 6;
        }
    }
}

int v4l2_open(V4L2Device *dev, const char *device_path,
              int width, int height) {
    memset(dev, 0, sizeof(*dev));
    dev->fd = -1;

    dev->fd = open(device_path, O_RDWR);
    if (dev->fd < 0) {
        perror("v4l2 open");
        return -1;
    }

    struct v4l2_capability cap;
    if (ioctl(dev->fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP");
        close(dev->fd);
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "Not a video capture device\n");
        close(dev->fd);
        return -1;
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(dev->fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT (RGB24)");
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        if (ioctl(dev->fd, VIDIOC_S_FMT, &fmt) < 0) {
            perror("VIDIOC_S_FMT (YUYV)");
            close(dev->fd);
            return -1;
        }
    }

    dev->width = fmt.fmt.pix.width;
    dev->height = fmt.fmt.pix.height;
    dev->pixel_format = fmt.fmt.pix.pixelformat;
    dev->stride = fmt.fmt.pix.bytesperline;
    if (dev->stride == 0) {
        if (dev->pixel_format == V4L2_PIX_FMT_YUYV)
            dev->stride = dev->width * 2;
        else
            dev->stride = dev->width * 3;
    }

    if (dev->pixel_format == V4L2_PIX_FMT_YUYV) {
        fprintf(stderr, "Warning: using YUYV format, converting to RGB24\n");
    } else if (dev->pixel_format == V4L2_PIX_FMT_MJPEG) {
        fprintf(stderr, "Warning: using MJPEG format, decoding to RGB24\n");
    } else if (dev->pixel_format != V4L2_PIX_FMT_RGB24 &&
               dev->pixel_format != V4L2_PIX_FMT_BGR24) {
        fprintf(stderr, "Warning: unsupported pixel format 0x%08x\n",
                dev->pixel_format);
    }

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = V4L2_BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(dev->fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        close(dev->fd);
        return -1;
    }

    dev->buffer_count = req.count;
    for (int i = 0; i < dev->buffer_count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(dev->fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            close(dev->fd);
            return -1;
        }

        size_t length = buf.length;
        void *ptr = mmap(NULL, length, PROT_READ | PROT_WRITE,
                         MAP_SHARED, dev->fd, buf.m.offset);
        if (ptr == MAP_FAILED) {
            perror("mmap");
            close(dev->fd);
            return -1;
        }

        dev->buffers[i] = ptr;
        dev->buffer_sizes[i] = length;

        if (ioctl(dev->fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            close(dev->fd);
            return -1;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(dev->fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        close(dev->fd);
        return -1;
    }

    dev->rgb_buffer_len = dev->width * dev->height * 3;
    dev->rgb_buffer = (uint8_t *)malloc((size_t)dev->rgb_buffer_len);
    if (!dev->rgb_buffer) {
        perror("malloc rgb_buffer");
        close(dev->fd);
        return -1;
    }
    dev->buffer = dev->rgb_buffer;
    dev->buffer_len = dev->rgb_buffer_len;

    return 0;
}

int v4l2_capture_frame(V4L2Device *dev) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(dev->fd, VIDIOC_DQBUF, &buf) < 0) {
        perror("VIDIOC_DQBUF");
        return -1;
    }

    if (buf.index >= (uint32_t)dev->buffer_count ||
        !dev->buffers[buf.index]) {
        fprintf(stderr, "Invalid buffer index %u\n", buf.index);
        return -1;
    }

    const uint8_t *frame = (const uint8_t *)dev->buffers[buf.index];
    if (dev->pixel_format == V4L2_PIX_FMT_YUYV) {
        yuyv_to_rgb24(frame, dev->rgb_buffer,
                      dev->width, dev->height, dev->stride);
    } else if (dev->pixel_format == V4L2_PIX_FMT_MJPEG) {
        if (buf.bytesused == 0 ||
            mjpeg_to_rgb24(frame, buf.bytesused, dev->rgb_buffer,
                           dev->width, dev->height) < 0) {
            fprintf(stderr, "Failed to decode MJPEG frame\n");
            return -1;
        }
    } else if (dev->pixel_format == V4L2_PIX_FMT_RGB24 ||
               dev->pixel_format == V4L2_PIX_FMT_BGR24) {
        copy_rgb24_packed(frame, dev->rgb_buffer,
                          dev->width, dev->height, dev->stride);
    } else {
        fprintf(stderr, "Unsupported pixel format 0x%08x\n",
                dev->pixel_format);
        return -1;
    }

    if (ioctl(dev->fd, VIDIOC_QBUF, &buf) < 0) {
        perror("VIDIOC_QBUF");
        return -1;
    }

    return 0;
}

int v4l2_get_exposure(V4L2Device *dev, int *exposure_out) {
    if (!dev || dev->fd < 0 || !exposure_out) return -1;

    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_EXPOSURE_ABSOLUTE;
    if (ioctl(dev->fd, VIDIOC_G_CTRL, &ctrl) < 0) {
        ctrl.id = V4L2_CID_EXPOSURE;
        if (ioctl(dev->fd, VIDIOC_G_CTRL, &ctrl) < 0) {
            perror("VIDIOC_G_CTRL (exposure)");
            return -1;
        }
    }

    *exposure_out = ctrl.value;
    return 0;
}

int v4l2_set_exposure(V4L2Device *dev, int exposure) {
    if (!dev || dev->fd < 0) return -1;

    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_EXPOSURE_AUTO;
    ctrl.value = V4L2_EXPOSURE_MANUAL;
    if (ioctl(dev->fd, VIDIOC_S_CTRL, &ctrl) < 0) {
        fprintf(stderr, "Warning: cannot set exposure to manual\n");
    }

    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_EXPOSURE_ABSOLUTE;
    ctrl.value = exposure;
    if (ioctl(dev->fd, VIDIOC_S_CTRL, &ctrl) < 0) {
        ctrl.id = V4L2_CID_EXPOSURE;
        ctrl.value = exposure;
        if (ioctl(dev->fd, VIDIOC_S_CTRL, &ctrl) < 0) {
            perror("VIDIOC_S_CTRL (exposure)");
            return -1;
        }
    }

    return 0;
}

void v4l2_close(V4L2Device *dev) {
    if (dev->fd >= 0) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(dev->fd, VIDIOC_STREAMOFF, &type);
        for (int i = 0; i < dev->buffer_count; i++) {
            if (dev->buffers[i]) {
                munmap(dev->buffers[i], dev->buffer_sizes[i]);
            }
        }
        close(dev->fd);
    }
    if (dev->rgb_buffer) {
        free(dev->rgb_buffer);
    }
    memset(dev, 0, sizeof(*dev));
}
