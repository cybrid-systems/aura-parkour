#ifndef PARKOUR_SAMPLE_H
#define PARKOUR_SAMPLE_H
#include <stddef.h>

#define PARKOUR_MAX_OBS 256

typedef struct {
    int kind;
    int x, w, h, flags;
} ParkourObs;

typedef struct {
    double gravity, jump_v, slide_h;
    double px, py, vx, vy;
    int state;
    int nobs;
    ParkourObs obs[PARKOUR_MAX_OBS];
    int accepted; /* 1 after at least one complete SNAP */
} ParkourSnap;

/* Parse text Soft wrote. The last complete SNAP replaces *out.
   A truncated trailing frame is ignored and the previous accepted
   snapshot is kept. Returns 1 if *out holds an accepted snapshot. */
int parkour_sample_parse(const char *text, size_t n, ParkourSnap *out);
#endif
