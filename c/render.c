#include "render.h"

#include <stdio.h>

enum { COLS = 64, ROWS = 20 };

static char kind_glyph(int k) {
    switch (k) {
    case 0: return 'v';
    case 1: return '=';
    case 2: return '#';
    case 3: return '+';
    case 4: return 'o';
    default: return '?';
    }
}

/* Project a world point. Camera is behind (+X forward) and above the player.
   Returns 0 when the point is behind the camera. */
static int project(const ParkourSnap *s, double wx, double wy, double wz,
                   int *su, int *sv, double *depth) {
    double dx = wx - (s->x - 6.0);
    if (dx < 0.25)
        return 0;
    double fz = 16.0 * (wz - s->z) / dx;
    double fy = 12.0 * (wy - (s->y + 2.0)) / dx;
    *su = (int)(COLS / 2 + fz);
    *sv = (int)(ROWS / 2 - fy);
    *depth = dx;
    return 1;
}

int parkour_render(const ParkourSnap *s, char *dst, size_t dst_n) {
    if (s == NULL || dst == NULL || dst_n == 0)
        return -1;
    char grid[ROWS][COLS];
    double zbuf[ROWS][COLS];
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            grid[r][c] = ' ';
            zbuf[r][c] = 1.0e9;
        }
    }
    for (int i = 0; i < s->nobs; i++) {
        const ParkourObs *o = &s->obs[i];
        /* Near face (min X) of the AABB. Viewport does not invent more faces. */
        double xs[4] = {o->x, o->x, o->x, o->x};
        double ys[4] = {o->y, o->y + o->h, o->y, o->y + o->h};
        double zs[4] = {o->z, o->z, o->z + o->d, o->z + o->d};
        int umin = COLS, umax = -1, vmin = ROWS, vmax = -1;
        double depth = 1.0e9;
        int ok = 0;
        for (int k = 0; k < 4; k++) {
            int su, sv;
            double dep;
            if (!project(s, xs[k], ys[k], zs[k], &su, &sv, &dep))
                continue;
            ok = 1;
            if (su < umin) umin = su;
            if (su > umax) umax = su;
            if (sv < vmin) vmin = sv;
            if (sv > vmax) vmax = sv;
            if (dep < depth) depth = dep;
        }
        if (!ok)
            continue;
        if (umin < 0) umin = 0;
        if (vmin < 0) vmin = 0;
        if (umax >= COLS) umax = COLS - 1;
        if (vmax >= ROWS) vmax = ROWS - 1;
        char g = kind_glyph(o->kind);
        for (int r = vmin; r <= vmax; r++) {
            for (int c = umin; c <= umax; c++) {
                if (depth < zbuf[r][c]) {
                    zbuf[r][c] = depth;
                    grid[r][c] = g;
                }
            }
        }
    }
    int pu, pv;
    double pd;
    if (project(s, s->x, s->y + 1.0, s->z, &pu, &pv, &pd) &&
        pu >= 0 && pu < COLS && pv >= 0 && pv < ROWS)
        grid[pv][pu] = '@';

    size_t used = 0;
    int n = snprintf(dst, dst_n,
                     "SOFT3D gravity=%g jump_v=%g slide_h=%g "
                     "pos=(%g,%g,%g) state=%d obs=%d\n",
                     s->gravity, s->jump_v, s->slide_h,
                     s->x, s->y, s->z, s->state, s->nobs);
    if (n < 0 || (size_t)n >= dst_n)
        return -1;
    used = (size_t)n;
    for (int r = 0; r < ROWS; r++) {
        if (used + (size_t)COLS + 1 >= dst_n)
            return -1;
        for (int c = 0; c < COLS; c++)
            dst[used++] = grid[r][c];
        dst[used++] = '\n';
    }
    dst[used] = '\0';
    return (int)used;
}
