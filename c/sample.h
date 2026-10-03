#ifndef PARKOUR_SAMPLE_H
#define PARKOUR_SAMPLE_H
#include <stddef.h>

#define PARKOUR_MAX_OBS 256

/* World-space box. Soft owns the numbers. kind is data, not a generator. */
typedef struct {
    int kind;
    int x, y, z;
    int w, h, d;
    int flags;
} ParkourObs;

typedef struct {
    double gravity, jump_v, slide_h;
    double x, y, z;
    double vx, vy, vz;
    int state;
    int score;
    int alive;
    int tick;
    int nobs;
    ParkourObs obs[PARKOUR_MAX_OBS];
    int accepted;
} ParkourSnap;

/* Last complete SNAP wins. A truncated tail does not replace it
   and does not invent boxes. Soft SCORE line is optional for M0 snaps. */
int parkour_sample_parse(const char *text, size_t n, ParkourSnap *out);
#endif
