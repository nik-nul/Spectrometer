#ifndef ICC_H
#define ICC_H

int icc_write_display_profile(const char *path,
                              double rx, double ry,
                              double gx, double gy,
                              double bx, double by,
                              double wx, double wy,
                              double gamma,
                              const char *description);

#endif
