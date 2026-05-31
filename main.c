#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <ctype.h>
#include <math.h>
#include <SDL2/SDL.h>

#include "lib/spectrometer.h"
#include "lib/calibration.h"
#include "lib/csv.h"
#include "lib/colorimetry.h"
#include "input/v4l2.h"
#include "input/image_loader.h"
#include "output/sdl_display.h"

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -d <device>   V4L2 device path (e.g. /dev/video0)\n");
    fprintf(stderr, "  -i <file>     Input image file (PPM P6 format)\n");
    fprintf(stderr, "  -o <file>     Output CSV file\n");
    fprintf(stderr, "  -w <width>    Capture width (default 1920)\n");
    fprintf(stderr, "  -h <height>   Capture height (default 1080)\n");
    fprintf(stderr, "  -e <value>    Manual exposure value (device-specific, usually 1 to 5000, default 1)\n");
    fprintf(stderr, "  -E <step>     Exposure step for [/] (default 100)\n");
    fprintf(stderr, "  -g <gamma>    Gamma correction (default 2.2)\n");
    fprintf(stderr, "  -G            Enable gamma correction\n");
    fprintf(stderr, "  -C            Calibration mode (press v to start)\n");
    fprintf(stderr, "  -K <file>     Calibration file path (default calibration.txt)\n");
    fprintf(stderr, "  -N            Disable calibration loading\n");
    fprintf(stderr, "  -X <start_x>  ROI start X 0-1000 (default 0)\n");
    fprintf(stderr, "  -x <end_x>    ROI end X 0-1000 (default 1000)\n");
    fprintf(stderr, "  -Y <start_y>  ROI start Y 0-100 (default 45)\n");
    fprintf(stderr, "  -y <size_y>   ROI height 0-100 (default 10)\n");
    fprintf(stderr, "  -f <filter>   Filter strength 0-100 (default 30)\n");
    fprintf(stderr, "  -u <speed>    Rising speed 0-100 (default 30)\n");
    fprintf(stderr, "  -v <speed>    Falling speed 0-100 (default 30)\n");
    fprintf(stderr, "  -n <min>      Wavelength min nm (default 270, ignored with calibration)\n");
    fprintf(stderr, "  -m <max>      Wavelength max nm (default 1200, ignored with calibration)\n");
    fprintf(stderr, "  -r            Flip spectrum direction (mirror X)\n");
    fprintf(stderr, "  -c            Disable color rendering\n");
    fprintf(stderr, "  -p            Disable peak detection\n");
    fprintf(stderr, "  -s            Enable headless mode (no SDL)\n");
    fprintf(stderr, "  -l <file>     Enable CSV logging (append to file)\n");
    fprintf(stderr, "  -R <file>     Compute CRI/CCT from spectrum CSV and exit\n");
    fprintf(stderr, "  -F            Use external ref CSV files (ref/*.csv)\n");
    fprintf(stderr, "  -T            Disable CCT/Ra overlay on SDL\n");
    fprintf(stderr, "  -Z            Gamut test mode (show color patch window)\n");
    fprintf(stderr, "  -D <px>       Dest graph width (default 1920)\n");
    fprintf(stderr, "  -H <px>       Dest graph height (default 1080)\n");
    fprintf(stderr, "  -?            This help\n");
}

static double wall_seconds(void) {
    return (double)time(NULL);
}

static uint32_t poll_cli_keymask(void) {
    uint32_t mask = 0;
    fd_set rfds;
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    int ret = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
    if (ret <= 0) return 0;

    char buf[64];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0) return 0;
    for (ssize_t i = 0; i < n; i++) {
        char c = (char)tolower((unsigned char)buf[i]);
        if (c == 's') mask |= SDL_KEYMASK_SAVE;
        else if (c == 'v') mask |= SDL_KEYMASK_CALIBRATE;
        else if (c == 'c') mask |= SDL_KEYMASK_COLORIMETRY;
        else if (c == ' ') mask |= SDL_KEYMASK_PAUSE;
    }
    return mask;
}

static void make_snapshot_name(char *out, size_t out_len, int index) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    snprintf(out, out_len,
             "spectrum_%04d%02d%02d_%02d%02d%02d_%02d.ppm",
             tm_now.tm_year + 1900,
             tm_now.tm_mon + 1,
             tm_now.tm_mday,
             tm_now.tm_hour,
             tm_now.tm_min,
             tm_now.tm_sec,
             index);
}

static void make_calibration_base(char *out, size_t out_len) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    snprintf(out, out_len,
             "calibration_%04d%02d%02d_%02d%02d%02d",
             tm_now.tm_year + 1900,
             tm_now.tm_mon + 1,
             tm_now.tm_mday,
             tm_now.tm_hour,
             tm_now.tm_min,
             tm_now.tm_sec);
}

static int prompt_calibration_points(const SpectrometerContext *ctx,
                                     CalibrationPoint *points,
                                     int max_points) {
    int peaks[16];
    int peak_count = spec_find_peaks(ctx, peaks, 16);
    if (peak_count <= 0) {
        printf("No peaks detected for calibration.\n");
        return 0;
    }

    printf("Calibration mode: enter true wavelengths for peaks.\n");
    printf("Press Enter to skip a peak, or 'q' to finish.\n");

    int count = 0;
    for (int i = 0; i < peak_count && count < max_points; i++) {
        float x = spec_bin_to_x(ctx, peaks[i]);
        float nm_est = spec_x_to_nanometers(ctx, x);
        char line[64];

        printf("Peak %d at %.1f nm -> true nm: ", i + 1, nm_est);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        if (line[0] == 'q' || line[0] == 'Q') break;
        if (line[0] == '\n' || line[0] == '\r') continue;

        char *end = NULL;
        float nm_true = strtof(line, &end);
        if (end == line) {
            printf("Invalid input, skipped.\n");
            continue;
        }

        points[count].bin = peaks[i];
        points[count].nm = nm_true;
        count++;
    }

    return count;
}

static int load_and_apply_calibration(const char *path,
                                      SpectrometerContext *ctx) {
    if (!path || !ctx) return -1;

    CalibrationPoint points[64];
    int count = 0;
    float nm_start = 0.0f;
    float nm_per_x = 0.0f;

    if (calibration_load(path, &nm_start, &nm_per_x,
                         points, 64, &count) < 0) {
        return -1;
    }

    if (nm_per_x <= 0.0f && count >= 2) {
        if (calibration_compute(ctx, points, count,
                                &nm_start, &nm_per_x) < 0) {
            return -1;
        }
    }

    if (nm_per_x > 0.0f) {
        if (spec_apply_calibration(ctx, nm_start, nm_per_x) == 0) {
            printf("Calibration loaded: nm_start=%.2f nm, nm_per_x=%.6f\n",
                   nm_start, nm_per_x);
            return 0;
        }
    }

    return -1;
}

int main(int argc, char **argv) {
    const char *v4l2_device = NULL;
    const char *image_file = NULL;
    const char *csv_output = NULL;
    const char *csv_log = NULL;
    const char *cri_csv_input = NULL;
    int show_colorimetry = 1;
    int use_external_refs = 0;
    int gamut_mode = 0;
    int cap_width = 1920, cap_height = 1080;
    int exposure = 201;
    int exposure_step = 100;
    float target_fps = 20.0f;
    int show_sdl = 1;
    int snapshot_index = 0;
    int calibration_mode = 0;
    int use_calibration = 1;
    const char *calib_path = "calibration.txt";
    int calibrating = 0;

    SpectrometerConfig cfg;
    spec_init_config(&cfg);

    int opt;
    while ((opt = getopt(argc, argv, "d:i:o:w:h:e:E:g:GX:x:Y:y:f:u:v:n:m:rcpD:H:sl:R:FTZ?CK:N")) != -1) {
        switch (opt) {
            case 'd': v4l2_device = optarg; break;
            case 'i': image_file = optarg; break;
            case 'o': csv_output = optarg; break;
            case 'w': cap_width = atoi(optarg); break;
            case 'h': cap_height = atoi(optarg); break;
            case 'e': exposure = atoi(optarg); break;
            case 'E': exposure_step = atoi(optarg); break;
            case 'g': cfg.gamma = (float)atof(optarg); cfg.gamma_enable = true; break;
            case 'G': cfg.gamma_enable = true; break;
            case 'X': cfg.start_x = atoi(optarg); break;
            case 'x': cfg.end_x = atoi(optarg); break;
            case 'Y': cfg.start_y = atoi(optarg); break;
            case 'y': cfg.size_y = atoi(optarg); break;
            case 'f': cfg.filter = atoi(optarg); break;
            case 'u': cfg.rising_speed = atoi(optarg); break;
            case 'v': cfg.falling_speed = atoi(optarg); break;
            case 'n': cfg.nm_min = (float)atof(optarg); break;
            case 'm': cfg.nm_max = (float)atof(optarg); break;
            case 'r': cfg.flip = false; break;
            case 'c': cfg.use_colors = false; break;
            case 'p': cfg.show_peaks = false; break;
            case 's': show_sdl = 0; break;
            case 'l': csv_log = optarg; break;
            case 'R': cri_csv_input = optarg; break;
            case 'F': use_external_refs = 1; break;
            case 'T': show_colorimetry = 0; break;
            case 'Z': gamut_mode = 1; break;
            case 'D': cfg.dest_width = atoi(optarg); break;
            case 'H': cfg.dest_height = atoi(optarg); break;
            case 'C': calibration_mode = 1; break;
            case 'K': calib_path = optarg; break;
            case 'N': use_calibration = 0; break;
            case '?': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    colorimetry_set_use_external_refs(use_external_refs);

    if (gamut_mode && !show_sdl) {
        fprintf(stderr, "Gamut mode requires SDL display, enabling SDL.\n");
        show_sdl = 1;
    }

    if (calibration_mode && !show_sdl) {
        fprintf(stderr, "Calibration mode requires SDL display, enabling SDL.\n");
        show_sdl = 1;
    }

    if (cri_csv_input) {
        CRIResult res;
        if (colorimetry_compute_cri_from_csv(cri_csv_input, &res) == 0) {
            colorimetry_print_result(&res);
            return 0;
        }
        fprintf(stderr, "Failed to compute CRI/CCT from CSV: %s\n",
                cri_csv_input);
        return 1;
    }

    if (!v4l2_device && !image_file) {
        fprintf(stderr, "Error: specify either -d (V4L2) or -i (image)\n");
        print_usage(argv[0]);
        return 1;
    }

    SpectrometerContext ctx;
    spec_init_context(&ctx, &cfg);

    SDLDisplay dpy;
    if (show_sdl) {
        if (sdl_display_init(&dpy, cfg.dest_width,
                             cfg.dest_height) < 0) {
            show_sdl = 0;
        }
        if (show_sdl) {
            dpy.show_colorimetry = show_colorimetry;
            if (gamut_mode) {
                if (sdl_display_enable_gamut_mode(&dpy, 320, 320) < 0) {
                    fprintf(stderr, "Failed to enable gamut mode\n");
                    gamut_mode = 0;
                }
            }
        }
    }

    if (image_file) {
        ImageData img;
        if (image_load(image_file, &img) < 0) {
            fprintf(stderr, "Failed to load image: %s\n",
                    image_file);
            if (show_sdl) sdl_display_close(&dpy);
            return 1;
        }
        printf("Loaded image: %dx%d\n", img.width, img.height);

        spec_set_source_params(&ctx, img.width, img.height, &cfg);
        if (use_calibration && !calibration_mode) {
            load_and_apply_calibration(calib_path, &ctx);
        }
        spec_set_running_mode_params(&ctx, &cfg);

        spec_process_frame(&ctx, img.data, img.width, img.height);

        if (cfg.reference_enabled) {
            spec_set_reference(&ctx);
            spec_process_frame(&ctx, img.data, img.width,
                               img.height);
        }

        if (csv_output) {
            if (csv_write_spectrum(csv_output, &ctx, ',') < 0) {
                fprintf(stderr, "Failed to write CSV: %s\n",
                        csv_output);
            } else {
                printf("CSV written to %s\n", csv_output);
            }
        }

        if (show_sdl) {
            sdl_display_render(&dpy, &ctx);
            SDL_Delay(5000);
        }

        image_free(&img);
    }

    if (v4l2_device) {
        V4L2Device dev;
        if (v4l2_open(&dev, v4l2_device, cap_width, cap_height) < 0) {
            fprintf(stderr, "Failed to open V4L2 device: %s\n",
                    v4l2_device);
            if (show_sdl) sdl_display_close(&dpy);
            return 1;
        }
        printf("V4L2 device opened: %dx%d\n", dev.width, dev.height);

        if (v4l2_set_exposure(&dev, exposure) < 0) {
            fprintf(stderr, "Failed to set exposure: %d\n",
                    exposure);
        } else {
            printf("Exposure set to %d\n", exposure);
        }

        if (v4l2_get_exposure(&dev, &exposure) == 0) {
            printf("Current exposure: %d\n", exposure);
        }

        spec_set_source_params(&ctx, dev.width, dev.height, &cfg);
        if (use_calibration && !calibration_mode) {
            load_and_apply_calibration(calib_path, &ctx);
        }
        spec_set_running_mode_params(&ctx, &cfg);

        int total_frames = 0;
        int fps_frames = 0;
        int paused = 0;
        double last_log = wall_seconds();
        FILE *csv_log_fp = NULL;

        if (csv_log) {
            csv_log_fp = fopen(csv_log, "a");
            if (!csv_log_fp) {
                fprintf(stderr, "Cannot open CSV log: %s\n",
                        csv_log);
                csv_log = NULL;
            }
        }

        while (show_sdl ? dpy.running : (total_frames < 100)) {
            if (!paused) {
                if (v4l2_capture_frame(&dev) < 0) break;
                spec_process_frame(&ctx, dev.buffer, dev.width,
                                   dev.height);
                total_frames++;
                fps_frames++;
            } else if (!show_sdl) {
                usleep(10000);
            }

            uint32_t key_mask = poll_cli_keymask();
            if (show_sdl) {
                sdl_display_render(&dpy, &ctx);
                key_mask |= dpy.key_mask;
            }
            if (key_mask) {
                if (key_mask & SDL_KEYMASK_PAUSE) {
                    paused = !paused;
                    printf("%s\n", paused ? "Paused" : "Resumed");
                    fflush(stdout);
                }
                int delta = 0;
                if (key_mask & SDL_KEYMASK_EXPOSURE_DEC)
                    delta = -exposure_step;
                if (key_mask & SDL_KEYMASK_EXPOSURE_INC)
                    delta = exposure_step;

                if (delta != 0) {
                    if (exposure < 0 &&
                        v4l2_get_exposure(&dev, &exposure) < 0) {
                        fprintf(stderr,
                                "Cannot read current exposure\n");
                    } else if (exposure >= 0) {
                        exposure += delta;
                        if (v4l2_set_exposure(&dev, exposure) < 0) {
                            fprintf(stderr,
                                    "Failed to set exposure: %d\n",
                                    exposure);
                        } else {
                            printf("\nExposure: %d\n", exposure);
                            fflush(stdout);
                        }
                    }
                }

                if (key_mask & SDL_KEYMASK_SAVE) {
                    char path[64];
                    make_snapshot_name(path, sizeof(path),
                                       snapshot_index++);
                    if (show_sdl && sdl_display_save_ppm(&dpy, path) == 0) {
                        printf("\nSaved spectrum image: %s\n", path);
                    } else if (show_sdl) {
                        fprintf(stderr,
                                "\nFailed to save spectrum image\n");
                    }
                    size_t len = strlen(path);
                    if (len >= 3) {
                        strcpy(path + len - 3, "csv");
                    }
                    if (csv_write_spectrum(path, &ctx, ',') < 0) {
                        fprintf(stderr, "Failed to write CSV: %s\n",
                                csv_output);
                    } else {
                        printf("CSV written to %s\n", csv_output);
                    }
                    fflush(stdout);
                }

                if (key_mask & SDL_KEYMASK_COLORIMETRY) {
                    CRIResult res;
                    printf("\n");
                    if (colorimetry_compute_cri_from_ctx(&ctx, &res) == 0) {
                        colorimetry_print_result(&res);
                    } else {
                        fprintf(stderr, "Failed to compute CRI/CCT from live data\n");
                    }
                    fflush(stdout);
                }

                if (gamut_mode && (key_mask & SDL_KEYMASK_GAMUT_SAMPLE)) {
                    int stage = -1;
                    double x = 0.0;
                    double y = 0.0;
                    if (sdl_display_gamut_sample(&dpy, &ctx,
                                                 &stage, &x, &y) == 0) {
                        const char *label = stage == 0 ? "R"
                                           : stage == 1 ? "G"
                                           : stage == 2 ? "B" : "?";
                        printf("\nGamut sample %s: x=%.4f y=%.4f\n",
                               label, x, y);
                        if (dpy.gamut_metrics_valid) {
                            for (int i = 0; i < 3; i++) {
                                int area_pct = (int)lround(
                                    dpy.gamut_metrics[i].area_ratio * 100.0);
                                int cov_pct = (int)lround(
                                    dpy.gamut_metrics[i].coverage_ratio * 100.0);
                                if (area_pct < 0) area_pct = 0;
                                if (cov_pct < 0) cov_pct = 0;
                                printf("%s A%d C%d\n",
                                       i == 0 ? "sRGB" :
                                       i == 1 ? "AdobeRGB" : "P3",
                                       area_pct, cov_pct);
                            }
                        }
                    } else {
                        fprintf(stderr, "Failed to sample gamut data\n");
                    }
                    fflush(stdout);
                }

                if (gamut_mode && (key_mask & SDL_KEYMASK_GAMUT_RESET)) {
                    sdl_display_gamut_reset(&dpy);
                    printf("\nGamut samples reset\n");
                    fflush(stdout);
                }

                if (calibration_mode &&
                    (key_mask & SDL_KEYMASK_CALIBRATE) &&
                    !calibrating) {
                        calibrating = 1;

                        CalibrationPoint points[32];
                        int point_count = prompt_calibration_points(&ctx,
                                                                    points, 32);
                        if (point_count >= 2) {
                            float nm_start = 0.0f;
                            float nm_per_x = 0.0f;
                            if (calibration_compute(&ctx, points, point_count,
                                                    &nm_start, &nm_per_x) == 0 &&
                                spec_apply_calibration(&ctx,
                                                       nm_start, nm_per_x) == 0) {
                                if (calibration_save(calib_path,
                                                     nm_start, nm_per_x,
                                                     points, point_count) == 0) {
                                    printf("Calibration saved: %s\n", calib_path);
                                } else {
                                    fprintf(stderr, "Failed to save calibration\n");
                                }

                                char base[64];
                                char csv_path[80];
                                char ppm_path[80];
                                make_calibration_base(base, sizeof(base));
                                snprintf(csv_path, sizeof(csv_path),
                                         "%s.csv", base);
                                snprintf(ppm_path, sizeof(ppm_path),
                                         "%s.ppm", base);

                                if (csv_write_spectrum(csv_path, &ctx, ',') == 0) {
                                    printf("Saved calibration CSV: %s\n",
                                           csv_path);
                                } else {
                                    fprintf(stderr, "Failed to save CSV\n");
                                }

                                if (sdl_display_save_ppm(&dpy, ppm_path) == 0) {
                                    printf("Saved calibration image: %s\n",
                                           ppm_path);
                                } else {
                                    fprintf(stderr, "Failed to save PPM\n");
                                }
                            } else {
                                fprintf(stderr, "Calibration compute failed\n");
                            }
                        } else {
                            fprintf(stderr, "Not enough points for calibration\n");
                        }
                        fflush(stdout);
                        calibrating = 0;
                    }
                }
                SDL_Delay(16);
            }

            double now = wall_seconds();
            double elapsed = now - last_log;
                 if (elapsed >= 1.0 && fps_frames > 0) {
                  float fps = (float)(fps_frames / elapsed);
                printf("\rFrames: %d, FPS: %.1f   ",
                       fps_frames,
                      fps);
                fflush(stdout);
                  spec_set_running_mode_params_fps(&ctx, &cfg,
                                    fps, target_fps);
                last_log = now;
                fps_frames = 0;

                if (csv_log_fp) {
                    csv_write_spectrum_fp(csv_log_fp, &ctx, ',');
                    fflush(csv_log_fp);
                }
            }
        printf("\n");

        if (csv_output) {
            if (csv_write_spectrum(csv_output, &ctx, ',') < 0) {
                fprintf(stderr, "Failed to write CSV: %s\n",
                        csv_output);
            } else {
                printf("CSV written to %s\n", csv_output);
            }
        }

        if (csv_log_fp) fclose(csv_log_fp);
        v4l2_close(&dev);
    }

    if (show_sdl) sdl_display_close(&dpy);
    return 0;
}
