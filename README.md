# Spectrometer

A real-time spectrum analyzer & CRI calculator for linux, using V4L2 and providing CSV/PPM output with SDL2-based real-time visualization. 

本项目是”光度学与色度学“[19003580]课程的课程项目。

<img width="1920" height="940" alt="1" src="https://github.com/user-attachments/assets/a0826ecb-ac34-4427-93ea-020a5d5363f8" />

<img width="1920" height="940" alt="3" src="https://github.com/user-attachments/assets/840e4e47-bc28-4ecd-b53d-5ec76d3e13c3" />

## Features

- **Live capture** from any V4L2-compatible camera (USB webcam, industrial camera)
- **Single-image analysis** from PPM P6 or JPEG files
- **Interactive wavelength calibration** — identify known spectral lines in calibration mode (`-C`) to compute nm/pixel mapping
- **ROI configuration** — adjustable start/end X and Y position/height
- **Signal processing**:
  - Per-column RGB-weighted accumulation over ROI
  - Low-pass digital filtering
  - Rise/fall smoothing
  - Gamma correction for jpeg files
- **Peak and dip detection** with on-screen markers
- **Wavelength-to-color rendering**
- **Colorimetry** — CCT + CRI (R1–R15, Ra) overlay and CSV computation
- **Interactive controls** (in SDL window):
  - `[` / `]` — decrease/increase exposure
  - `s` — save spectrum snapshot (PPM + CSV)
  - `v` — trigger calibration (in `-C` mode) (specify the expected result in CLI)
  - `c` — compute CCT/CRI once and print to CLI
  - `space` — pause/resume
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
  -G            Enable gamma correction
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
  -R <file>     Compute CRI/CCT from spectrum CSV and exit
  -F            Use external ref CSV files (ref/*.csv)
  -T            Disable CCT/Ra overlay on SDL
  -Z            Gamut test mode (show color patch window)
  -D <px>       Dest graph width (default 1920)
  -H <px>       Dest graph height (default 1080)
  -?            This help
```

### Examples

**Live camera with default settings:**
```bash
./spectrometer -d /dev/video2
# Press space to pause/resume.
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

**Compute CRI/CCT from CSV:**
```bash
./spectrometer -R spectrum.csv
```

### Colorimetry Notes

The SDL overlay shows CCT and Ra by default. Update rate is controlled by
macros in [output/sdl_display.h](output/sdl_display.h). The full method and
tables are documented in [colorimetry.md](colorimetry.md).
By default, the reference CSV tables are embedded in the binary; use `-F`
to read them from `ref/` instead.

## Architecture

```
main.c                     — CLI parsing, event loop, integration
├── lib/
│   ├── spectrometer.c/h   — Core spectrum processing pipeline
│   ├── wavelength.c/h     — Wavelength → RGB color mapping
│   ├── calibration.c/h    — Calibration data load/save/compute
│   ├── colorimetry.c/h     — CCT/CRI computation
│   └── csv.c/h            — CSV spectrum export
├── input/
│   ├── v4l2.c/h           — V4L2 camera capture backend
│   └── image_loader.c/h   — PPM/JPEG image loader
├── output/
│   └── sdl_display.c/h    — SDL2 real-time visualization
└── ref/
    ├── CIE_xyz_1931_2deg.csv — CIE 1931 2° CMFs
    ├── CIE_srf_cri.csv        — CRI R1–R15 reflectance tables
    └── S.csv                  — CIE daylight basis functions
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
If `calibration.txt` is missing, the embedded calibration table is used.

## Notes

This project was built and tested based on a homemade spectrometer model designed and sold by 窗台小圃, which can be found on platforms such as Taobao and Bilibili. The project references some logic of 窗台小圃's software, whose parts of the code were taken from by [Theremino Spectrometer](https://www.theremino.com/). However, no code from those two project was reused, as those were developed using VB and DirectShow, among other tech stacks.
