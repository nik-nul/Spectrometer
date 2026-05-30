CC      ?= gcc
LD      ?= ld
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
REFDIR  = ref
BUILDDIR= build

SRCS = main.c \
       $(LIBDIR)/spectrometer.c \
       $(LIBDIR)/wavelength.c \
	$(LIBDIR)/calibration.c \
	$(LIBDIR)/colorimetry.c \
       $(LIBDIR)/csv.c \
       $(INDIR)/v4l2.c \
       $(INDIR)/image_loader.c \
       $(OUTDIR)/sdl_display.c

REF_FILES = $(REFDIR)/CIE_xyz_1931_2deg.csv \
	     $(REFDIR)/CIE_srf_cri.csv \
	     $(REFDIR)/S.csv
REF_OBJS = $(REF_FILES:%=$(BUILDDIR)/%.o)

CALIB_FILE = calibration.txt
CALIB_OBJ = $(BUILDDIR)/calibration.txt.o

OBJS = $(SRCS:%=$(BUILDDIR)/%.o) $(REF_OBJS) $(CALIB_OBJ)
TARGET = spectrometer

.PHONY: all clean

all: $(BUILDDIR) $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)/$(LIBDIR) \
	         $(BUILDDIR)/$(INDIR) \
	         $(BUILDDIR)/$(OUTDIR) \
	         $(BUILDDIR)/$(REFDIR)

$(BUILDDIR)/$(REFDIR):
	mkdir -p $@

$(BUILDDIR)/%.c.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(JPEG_CFLAGS) -I$(SRCDIR) -c -o $@ $<


$(BUILDDIR)/$(REFDIR)/%.csv.o: $(REFDIR)/%.csv | $(BUILDDIR)/$(REFDIR)
	$(LD) -r -b binary -o $@ $<

$(BUILDDIR)/calibration.txt.o: $(CALIB_FILE) | $(BUILDDIR)
	$(LD) -r -b binary -o $@ $<

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS) $(SDL_LDFLAGS) $(JPEG_LDFLAGS)

clean:
	rm -rf $(BUILDDIR) $(TARGET)
