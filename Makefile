CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2 -g
LDFLAGS ?= -lm

SDL_CFLAGS  != pkg-config --cflags sdl2 2>/dev/null || echo ""
SDL_LDFLAGS != pkg-config --libs sdl2 2>/dev/null || echo "-lSDL2"
JPEG_CFLAGS != pkg-config --cflags libjpeg 2>/dev/null || echo ""
JPEG_LDFLAGS != pkg-config --libs libjpeg 2>/dev/null || echo "-ljpeg"

SRCDIR  = .
LIBDIR  = lib
INDIR   = input
OUTDIR  = output
BUILDDIR= build

SRCS = main.c \
       $(LIBDIR)/spectrometer.c \
       $(LIBDIR)/wavelength.c \
	$(LIBDIR)/calibration.c \
       $(LIBDIR)/csv.c \
       $(INDIR)/v4l2.c \
       $(INDIR)/image_loader.c \
       $(OUTDIR)/sdl_display.c

OBJS = $(SRCS:%=$(BUILDDIR)/%.o)
TARGET = spectrometer

.PHONY: all clean

all: $(BUILDDIR) $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)/$(LIBDIR) \
	         $(BUILDDIR)/$(INDIR) \
	         $(BUILDDIR)/$(OUTDIR)

$(BUILDDIR)/%.c.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(JPEG_CFLAGS) -I$(SRCDIR) -c -o $@ $<

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS) $(SDL_LDFLAGS) $(JPEG_LDFLAGS)

clean:
	rm -rf $(BUILDDIR) $(TARGET)
