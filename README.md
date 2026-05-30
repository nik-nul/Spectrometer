# Spectrometer

A real-time spectrum analyzer for linux, using V4L2 and providing CSV/PPM output with SDL2-based real-time visualization.

本项目是”光度学与色度学“课程的课程项目。

<img width="1920" height="940" alt="Image" src="https://github.com/user-attachments/assets/1434a1f2-86ab-4ba8-9994-76b5e7ec0239" />

## Features

- **Live capture** from any V4L2-compatible camera (USB webcam, industrial camera)
- **Single-image analysis** from PPM P6 or JPEG files
- **Interactive wavelength calibration** — identify known spectral lines in calibration mode (`-C`) to compute nm/pixel mapping
- **ROI configuration** — adjustable start/end X and Y position/height
- **Signal processing**:
  - Per-column RGB-weighted accumulation over ROI
  - Low-pass digital filtering
  - Rise/fall smoothing
  - Gamma correction for jepg files
- **Peak and dip detection** with on-screen markers
- **Wavelength-to-color rendering**
- **Interactive controls** (in SDL window):
  - `[` / `]` — decrease/increase exposure
  - `s` — save spectrum snapshot (PPM + CSV)
  - `v` — trigger calibration (in `-C` mode) (specify the expected result in CLI)
- **CSV export** for post-processing
- **Headless mode** (`-s`) for automated/embedded operation

## Dependencies

| Package        | Purpose                     |
|----------------|-----------------------------|
| SDL2           | Real-time display           |
| libv4l2        | Camera capture              |
| libjpeg        | JPEG image loading          |

### Install (Debian/Ubuntu)

```bash
sudo apt install build-essential libsdl2-dev libv4l-dev libjpeg-dev pkg-config
```

## Build

```bash
make
```

The binary `spectrometer` is produced in the project root.

## Usage

```text
Usage: ./spectrometer [options]
Options:
  -d <device>   V4L2 device path (e.g. /dev/video0)
  -i <file>     Input image file (PPM P6 format)
  -o <file>     Output CSV file
  -w <width>    Capture width (default 1920)
  -h <height>   Capture height (default 1080)
  -e <value>    Manual exposure value (device-specific, default 201)
  -E <step>     Exposure step for [/] (default 100)
  -g <gamma>    Gamma correction (default 2.2)
  -G            Disable gamma correction
  -C            Calibration mode (press v to start)
  -K <file>     Calibration file path (default calibration.txt)
  -N            Disable calibration loading
  -X <start_x>  ROI start X 0-1000 (default 0)
  -x <end_x>    ROI end X 0-1000 (default 1000)
  -Y <start_y>  ROI start Y 0-100 (default 45)
  -y <size_y>   ROI height 0-100 (default 10)
  -f <filter>   Filter strength 0-100 (default 30)
  -u <speed>    Rising speed 0-100 (default 30)
  -v <speed>    Falling speed 0-100 (default 30)
  -n <min>      Wavelength min nm (default 270, ignored with calibration)
  -m <max>      Wavelength max nm (default 1200, ignored with calibration)
  -r            Flip spectrum direction (mirror X)
  -c            Disable color rendering
  -p            Disable peak detection
  -s            Enable headless mode (no SDL)
  -l <file>     Enable CSV logging (append to file)
  -D <px>       Dest graph width (default 1920)
  -H <px>       Dest graph height (default 1080)
  -?            This help
```

### Examples

**Live camera with default settings:**
```bash
./spectrometer -d /dev/video2
```

**Analyze a single PPM image and save CSV:**
```bash
./spectrometer -i spectrum.ppm -o spectrum.csv
```

**Calibration mode with a known light source (e.g. fluorescent Hg lines):**
```bash
./spectrometer -d /dev/video2 -C
# Press 'v' in the SDL window, then enter known wavelengths for observed peaks
```

**Headless logging to CSV:**
```bash
./spectrometer -d /dev/video0 -s -o log.csv
```

## Architecture

```
main.c                     — CLI parsing, event loop, integration
├── lib/
│   ├── spectrometer.c/h   — Core spectrum processing pipeline
│   ├── wavelength.c/h     — Wavelength → RGB color mapping
│   ├── calibration.c/h    — Calibration data load/save/compute
│   └── csv.c/h            — CSV spectrum export
├── input/
│   ├── v4l2.c/h           — V4L2 camera capture backend
│   └── image_loader.c/h   — PPM/JPEG image loader
└── output/
    └── sdl_display.c/h    — SDL2 real-time visualization
```

## Calibration File Format

```
CALIBRATION_V1
nm_start 176.848251
nm_per_x 0.532869
points 7
427 404.000000
486 436.000000
540 465.000000
581 487.000000
631 512.000000
692 546.000000
813 610.000000
```

The calibration maps pixel columns to wavelengths via `nm(pixel) = nm_start + pixel * nm_per_x`.

## Notes

This project was built and tested based on a homemade spectrometer model designed and sold by 花园小圃, which can be found on platforms such as Taobao and Bilibili. The project references some logic of 花园小圃's software, whose parts of the code were taken from by [Theremino Spectrometer](https://www.theremino.com/). However, no code from those two project was reused, as the those were developed using VB and DirectShow, among other tech stacks.
