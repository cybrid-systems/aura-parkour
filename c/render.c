#include "render.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* First-person runner viewport. Soft owns every obstacle AABB.
   Floor, ceiling, side walls, lane guides, and the runner silhouette are
   camera presentation only — they are not obstacles and never affect score. */

enum { COLS = 100, ROWS = 36, VROWS = 72 }; /* two samples per cell */

/* Terminal cells are ~2x taller than wide, so vertical scale is 2x. */
static const double CELL_Z = 0.0390;
static const double CELL_Y = 0.0390; /* one vertical sample; pairs become one cell */

enum {
    FACE_NONE = -1,
    FACE_NX = 0,
    FACE_PX = 1,
    FACE_NY = 2,
    FACE_PY = 3,
    FACE_NZ = 4,
    FACE_PZ = 5
};

typedef struct {
    char ch;
    unsigned char fr, fg, fb;
    unsigned char br, bg, bb;
} Cell;

typedef struct {
    double ex, ey, ez;
    double fx, fy, fz;
    double ux, uy, uz;
    double rx, ry, rz;
    double pitch;
} Cam;

static int clampi(int v, int lo, int hi) {
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static double dmin(double a, double b) { return a < b ? a : b; }

static void mix_fog(int *r, int *g, int *b, double t) {
    double f = t / (t + 7.0);
    if (f < 0.0)
        f = 0.0;
    if (f > 0.94)
        f = 0.94;
    *r = (int)(*r * (1.0 - f) + 6 * f);
    *g = (int)(*g * (1.0 - f) + 8 * f);
    *b = (int)(*b * (1.0 - f) + 18 * f);
}

static void cam_build(const ParkourSnap *s, Cam *c) {
    /* Locked behind the sprint, slightly above. Jump lags so the body
       rises in frame; slide drops the eye so the ceiling comes down.
       Lane z eases so a tap reads as a slide, not a teleport. */
    static int primed = 0;
    static int last_tick = -1;
    static double sz, sy;
    double target, pitch, bob = 0.0;
    if (s->state == 2) {
        target = s->y + 0.92;
        pitch = 0.58;
    } else if (s->y > 0.15 || s->state == 1) {
        target = s->y + 1.25;
        pitch = 0.20;
    } else {
        target = s->y + 2.05;
        pitch = 0.40;
        bob = 0.07 * sin(s->x * 2.4);
    }
    if (!primed || s->tick < last_tick || fabs(s->z - sz) > 3.0) {
        sz = s->z;
        sy = target;
        primed = 1;
    } else {
        sz += (s->z - sz) * 0.92;
        sy += (target - sy) * 0.42;
    }
    last_tick = s->tick;
    c->ex = s->x - 1.75;
    c->ez = sz;
    c->ey = sy + bob;
    c->pitch = pitch;
    if (c->ey < 0.45)
        c->ey = 0.45;
    {
        double cp = cos(c->pitch);
        double sp = sin(c->pitch);
        c->fx = cp;
        c->fy = -sp;
        c->fz = 0.0;
        c->ux = sp;
        c->uy = cp;
        c->uz = 0.0;
        c->rx = 0.0;
        c->ry = 0.0;
        c->rz = 1.0;
    }
}

static int project(const Cam *c, double x, double y, double z, int *su, int *sv,
                   double *depth) {
    double relx = x - c->ex;
    double rely = y - c->ey;
    double relz = z - c->ez;
    double zc = relx * c->fx + rely * c->fy + relz * c->fz;
    double yc = relx * c->ux + rely * c->uy + relz * c->uz;
    double xc = relx * c->rx + rely * c->ry + relz * c->rz;
    if (zc < 0.20)
        return 0;
    *su = (int)((COLS / 2.0) + (xc / zc) / CELL_Z);
    *sv = (int)((VROWS / 2.0) - (yc / zc) / CELL_Y);
    *depth = zc;
    return 1;
}

static int ray_box(double ox, double oy, double oz, double dx, double dy, double dz,
                   double minx, double miny, double minz, double maxx, double maxy,
                   double maxz, double *t_hit, int *face) {
    double o[3] = {ox, oy, oz};
    double d[3] = {dx, dy, dz};
    double b0[3] = {minx, miny, minz};
    double b1[3] = {maxx, maxy, maxz};
    double tmin = 0.12;
    double tmax = 80.0;
    int hit_face = FACE_NONE;
    for (int a = 0; a < 3; a++) {
        if (fabs(d[a]) < 1e-10) {
            if (o[a] < b0[a] || o[a] > b1[a])
                return 0;
            continue;
        }
        double inv = 1.0 / d[a];
        double t0 = (b0[a] - o[a]) * inv;
        double t1 = (b1[a] - o[a]) * inv;
        int f0 = a * 2;
        int f1 = a * 2 + 1;
        if (t0 > t1) {
            double tmp = t0;
            t0 = t1;
            t1 = tmp;
            int tf = f0;
            f0 = f1;
            f1 = tf;
        }
        if (t0 > tmin) {
            tmin = t0;
            hit_face = f0;
        }
        if (t1 < tmax)
            tmax = t1;
        if (tmin > tmax)
            return 0;
    }
    if (hit_face == FACE_NONE)
        return 0;
    *t_hit = tmin;
    *face = hit_face;
    return 1;
}

static int gap_at(const ParkourSnap *s, double x, double z, int *rim) {
    for (int i = 0; i < s->nobs; i++) {
        const ParkourObs *o = &s->obs[i];
        if (o->kind != 0)
            continue;
        double x0 = o->x, x1 = o->x + o->w;
        double z0 = o->z, z1 = o->z + o->d;
        if (x < x0 || x >= x1 || z < z0 || z >= z1)
            continue;
        double m = dmin(dmin(x - x0, x1 - x), dmin(z - z0, z1 - z));
        *rim = m < 0.28;
        return 1;
    }
    *rim = 0;
    return 0;
}

static const char *state_name(int st) {
    switch (st) {
    case 1:
        return "AIR";
    case 2:
        return "SLIDE";
    case 3:
        return "DEAD";
    default:
        return "RUN";
    }
}

static const char *lane_name(double z) {
    int zc = (int)floor(z + 0.5);
    switch (zc) {
    case -2:
        return "L2";
    case -1:
        return "L1";
    case 0:
        return "C";
    case 1:
        return "R1";
    case 2:
        return "R2";
    default:
        return "--";
    }
}

/* Solid framebuffer cell. Glyphs made the corridor look like an ASCII map;
   the shaded color is the pixel. ch/bg args are ignored except by stamp_text. */
static void paint(Cell *cell, char ch, int fr, int fg, int fb, int br, int bgc,
                  int bb) {
    (void)ch;
    (void)br;
    (void)bgc;
    (void)bb;
    unsigned char r = (unsigned char)clampi(fr, 0, 255);
    unsigned char g = (unsigned char)clampi(fg, 0, 255);
    unsigned char b = (unsigned char)clampi(fb, 0, 255);
    cell->ch = ' ';
    cell->fr = cell->br = r;
    cell->fg = cell->bg = g;
    cell->fb = cell->bb = b;
}

static void shade_floor(const ParkourSnap *s, double hx, double hz, double t,
                        Cell *cell) {
    int rim = 0;
    if (gap_at(s, hx, hz, &rim)) {
        int R = rim ? 150 : 8;
        int G = rim ? 24 : 4;
        int B = rim ? 28 : 10;
        mix_fog(&R, &G, &B, t);
        paint(cell, rim ? '#' : ' ', R, G, B, R / 5, G / 5, B / 5);
        return;
    }
    int ix = (int)floor(hx * 2.0);
    int iz = (int)floor(hz * 2.0 + 16.0);
    int tile = (ix + iz) & 1;
    double fx = hx - floor(hx);
    int seam = (fx < 0.06 || fx > 0.94);
    int R = tile ? 96 : 38;
    int G = tile ? 104 : 44;
    int B = tile ? 118 : 58;
    if (seam) {
        R = R * 3 / 5;
        G = G * 3 / 5;
        B = B * 3 / 5;
    }
    char ch = ' ';
    /* Lane guides converge toward the vanishing point. Current lane is lit. */
    static const double guides[5] = {-2.0, -1.0, 0.0, 1.0, 2.0};
    for (int i = 0; i < 5; i++) {
        double dzg = fabs(hz - guides[i]);
        if (dzg < 0.055) {
            int cur = fabs(guides[i] - s->z) < 0.5;
            R = cur ? 236 : 120;
            G = cur ? 188 : 150;
            B = cur ? 64 : 78;
            ch = '|';
            break;
        }
    }
    /* Short chevrons, not a near-field slab. */
    {
        double frac = hx - floor(hx);
        if (fabs(hz - s->z) < 0.10 && frac > 0.42 && frac < 0.58 && (ix % 4) == 0) {
            R = 255;
            G = 220;
            B = 80;
            ch = '^';
        }
    }
    {
        double dx = hx - s->x;
        double dz = hz - s->z;
        if (dx * dx + dz * dz < 0.55) {
            R /= 4;
            G /= 4;
            B /= 4;
            ch = '.';
        }
    }
    mix_fog(&R, &G, &B, t);
    paint(cell, ch, R, G, B, R / 4, G / 4, B / 4);
}

static void shade_ceil(double hx, double t, Cell *cell) {
    int panel = ((int)floor(hx)) % 4 == 0;
    int R = panel ? 48 : 22;
    int G = panel ? 52 : 26;
    int B = panel ? 70 : 40;
    mix_fog(&R, &G, &B, t * 0.85);
    paint(cell, panel ? '+' : '\'', R, G, B, R / 5, G / 5, B / 5);
}

static void shade_wall(double hx, double hy, double t, int right, Cell *cell) {
    int col = ((int)floor(hx)) & 1;
    int band = ((int)floor(hy * 2.0)) & 1;
    int R = right ? 42 : 34;
    int G = right ? 58 : 50;
    int B = right ? 96 : 88;
    if (col) {
        R += 22;
        G += 18;
        B += 16;
    }
    if (band)
        B += 10;
    if (hy < 0.45) {
        R += 28;
        G += 24;
        B += 10;
    }
    char ch = ' ';
    mix_fog(&R, &G, &B, t);
    paint(cell, ch, R, G, B, R / 5, G / 5, B / 5);
}

static void kind_rgb(int kind, int *R, int *G, int *B) {
    switch (kind) {
    case 1: /* beam — slide under */
        *R = 240;
        *G = 186;
        *B = 42;
        break;
    case 2: /* block — jump or dodge */
        *R = 214;
        *G = 78;
        *B = 46;
        break;
    case 3: /* pad */
        *R = 36;
        *G = 196;
        *B = 150;
        break;
    case 4: /* coin */
        *R = 255;
        *G = 214;
        *B = 48;
        break;
    default:
        *R = 180;
        *G = 186;
        *B = 200;
        break;
    }
}

static double face_mul(int face) {
    switch (face) {
    case FACE_NX:
        return 1.15; /* face rushing at the camera */
    case FACE_PX:
        return 0.28;
    case FACE_PY:
        return 1.05;
    case FACE_NY:
        return 0.30;
    default:
        return 0.48; /* lane sides, clearly darker than the front */
    }
}

static int on_rim(int face, double hx, double hy, double hz, double x0, double y0,
                  double z0, double x1, double y1, double z1) {
    double m = 1e9;
    if (face == FACE_NX || face == FACE_PX) {
        m = dmin(dmin(hy - y0, y1 - hy), dmin(hz - z0, z1 - hz));
    } else if (face == FACE_NY || face == FACE_PY) {
        m = dmin(dmin(hx - x0, x1 - hx), dmin(hz - z0, z1 - hz));
    } else {
        m = dmin(dmin(hx - x0, x1 - hx), dmin(hy - y0, y1 - hy));
    }
    return m < 0.22;
}

static char face_glyph(int kind, int face, int tick) {
    if (kind == 4) {
        static const char spin[] = "*+x*";
        return spin[tick & 3];
    }
    if (kind == 1)
        return '=';
    if (face == FACE_PY)
        return '=';
    if (face == FACE_NZ || face == FACE_PZ)
        return ':';
    return '#';
}

static void shade_box(int kind, int face, int rim, int tick, double t, Cell *cell) {
    int R, G, B;
    kind_rgb(kind, &R, &G, &B);
    double m = face_mul(face);
    R = (int)(R * m);
    G = (int)(G * m);
    B = (int)(B * m);
    if (rim) {
        R = clampi(R + 50, 0, 255);
        G = clampi(G + 46, 0, 255);
        B = clampi(B + 36, 0, 255);
    }
    mix_fog(&R, &G, &B, kind == 4 ? t * 0.22 : t * 0.50);
    if (kind == 4) {
        R = clampi(R + 30, 0, 255);
        G = clampi(G + 24, 0, 255);
    }
    paint(cell, face_glyph(kind, face, tick), R, G, B, R / 6, G / 6, B / 6);
}

static void sky_at(int row, Cell *cell) {
    double u = (double)row / (double)(VROWS - 1);
    int R = (int)(18 + 92.0 * u);
    int G = (int)(36 + 70.0 * u);
    int B = (int)(120 - 28.0 * u);
    paint(cell, ' ', R, G, B, R, G, B);
}

static void trace_cell(const ParkourSnap *s, const Cam *cam, int col, int row,
                      Cell *cell, double *zout) {
    double ndc_x = ((double)col - (COLS / 2.0) + 0.5) * CELL_Z;
    double ndc_y = ((VROWS / 2.0) - (double)row - 0.5) * CELL_Y;
    double dx = cam->fx + cam->ux * ndc_y + cam->rx * ndc_x;
    double dy = cam->fy + cam->uy * ndc_y + cam->ry * ndc_x;
    double dz = cam->fz + cam->uz * ndc_y + cam->rz * ndc_x;
    double len = sqrt(dx * dx + dy * dy + dz * dz);
    if (len < 1e-8) {
        sky_at(row, cell);
        *zout = 1.0e9;
        return;
    }
    dx /= len;
    dy /= len;
    dz /= len;

    double best = 1.0e9;
    int what = 0; /* 0 sky, 1 floor, 2 ceil, 3 wallL, 4 wallR, 5 box */
    int face = FACE_NONE;
    int obi = -1;
    const double wall_z = 4.15;
    const double ceil_y = 4.70;

    if (fabs(dz) > 1e-8) {
        double zp = dz > 0.0 ? wall_z : -wall_z;
        double t = (zp - cam->ez) / dz;
        if (t > 0.12 && t < best) {
            double hy = cam->ey + dy * t;
            double hx = cam->ex + dx * t;
            if (hy >= 0.0 && hy <= ceil_y && hx > cam->ex - 0.2) {
                best = t;
                what = dz > 0.0 ? 4 : 3;
            }
        }
    }
    if (dy < -1e-8) {
        double t = (0.0 - cam->ey) / dy;
        if (t > 0.08 && t < best) {
            double hx = cam->ex + dx * t;
            double hz = cam->ez + dz * t;
            if (hx > cam->ex - 0.05 && hz > -wall_z && hz < wall_z) {
                best = t;
                what = 1;
            }
        }
    }
    if (dy > 1e-8) {
        double t = (ceil_y - cam->ey) / dy;
        if (t > 0.08 && t < best) {
            double hx = cam->ex + dx * t;
            double hz = cam->ez + dz * t;
            if (hx > cam->ex && hz > -wall_z && hz < wall_z) {
                best = t;
                what = 2;
            }
        }
    }
    for (int i = 0; i < s->nobs; i++) {
        const ParkourObs *o = &s->obs[i];
        if (o->kind == 0)
            continue; /* gaps are holes in the floor, not solid crates */
        if (o->kind == 4 && (o->flags & 1))
            continue;
        if (o->w <= 0 || o->h <= 0 || o->d <= 0)
            continue;
        double t;
        int f;
        if (!ray_box(cam->ex, cam->ey, cam->ez, dx, dy, dz, (double)o->x,
                     (double)o->y, (double)o->z, (double)o->x + o->w,
                     (double)o->y + o->h, (double)o->z + o->d, &t, &f))
            continue;
        if (t < best) {
            best = t;
            what = 5;
            face = f;
            obi = i;
        }
    }

    *zout = best;
    if (what == 1) {
        shade_floor(s, cam->ex + dx * best, cam->ez + dz * best, best, cell);
    } else if (what == 2) {
        shade_ceil(cam->ex + dx * best, best, cell);
    } else if (what == 3 || what == 4) {
        shade_wall(cam->ex + dx * best, cam->ey + dy * best, best, what == 4, cell);
    } else if (what == 5 && obi >= 0) {
        const ParkourObs *o = &s->obs[obi];
        double hx = cam->ex + dx * best;
        double hy = cam->ey + dy * best;
        double hz = cam->ez + dz * best;
        int rim = on_rim(face, hx, hy, hz, o->x, o->y, o->z, o->x + o->w,
                         o->y + o->h, o->z + o->d);
        shade_box(o->kind, face, rim, s->tick, best, cell);
    } else {
        sky_at(row, cell);
        *zout = 1.0e9;
    }
}

static void stamp_disc(Cell grid[][COLS], double zbuf[][COLS], int cr, int cc,
                       int rad_r, int rad_c, double depth, char ch, int R, int G,
                       int B) {
    for (int r = cr - rad_r; r <= cr + rad_r; r++) {
        if (r < 0 || r >= VROWS)
            continue;
        for (int c = cc - rad_c; c <= cc + rad_c; c++) {
            if (c < 0 || c >= COLS)
                continue;
            double nr = rad_r > 0 ? (double)(r - cr) / (double)rad_r : 0.0;
            double nc = rad_c > 0 ? (double)(c - cc) / (double)rad_c : 0.0;
            if (nr * nr + nc * nc > 1.0)
                continue;
            if (depth >= zbuf[r][c])
                continue;
            zbuf[r][c] = depth;
            paint(&grid[r][c], ch, R, G, B, R / 5, G / 5, B / 5);
        }
    }
}

/* Viewport avatar. Not a Soft body and not an '@' map marker. */
static void limb(Cell grid[][COLS], double zbuf[][COLS], const Cam *cam,
                 double x0, double y0, double z0, double x1, double y1, double z1,
                 int rad, int R, int G, int B) {
    int c0, r0, c1, r1;
    double d0, d1;
    if (!project(cam, x0, y0, z0, &c0, &r0, &d0))
        return;
    if (!project(cam, x1, y1, z1, &c1, &r1, &d1))
        return;
    int steps = abs(c1 - c0);
    if (abs(r1 - r0) > steps)
        steps = abs(r1 - r0);
    if (steps < 1)
        steps = 1;
    if (steps > 48)
        steps = 48;
    /* Overlay the body. A world-depth test hides feet in the near floor. */
    double depth = 0.02;
    (void)d0;
    (void)d1;
    for (int i = 0; i <= steps; i++) {
        int c = c0 + (c1 - c0) * i / steps;
        int r = r0 + (r1 - r0) * i / steps;
        stamp_disc(grid, zbuf, r, c, rad, rad + 1, depth, ' ', R, G, B);
    }
}

static void stamp_runner(Cell grid[][COLS], double zbuf[][COLS], const ParkourSnap *s,
                         const Cam *cam) {
    double x = s->x + 0.05;
    double y = s->y;
    double z = s->z;
    double phase = s->x * 3.4;
    double swing = 0.0;
    double hip = 0.92;
    double head = 1.58;
    double foot_y = 0.02;
    double tuck_x = 0.0;
    if (s->state == 2) {
        hip = 0.32;
        head = 0.52;
        swing = 0.0;
        tuck_x = 0.35;
        foot_y = 0.02;
    } else if (s->state == 1 || s->y > 0.25) {
        hip = 0.78;
        head = 1.42;
        swing = 0.15;
        tuck_x = 0.28;
        foot_y = 0.42;
    } else {
        swing = sin(phase) * 0.38;
    }
    int jr = s->state == 3 ? 170 : 20;
    int jg = s->state == 3 ? 36 : 150;
    int jb = s->state == 3 ? 42 : 220;
    /* Torso, arms, legs, then head. Later stamps sit closer in depth. */
    limb(grid, zbuf, cam, x, y + hip * 0.55, z, x - 0.02, y + head - 0.34, z, 2, jr, jg, jb);
    limb(grid, zbuf, cam, x, y + head - 0.40, z, x - swing * 0.5, y + hip * 0.7, z - 0.28,
         1, jr + 15, jg + 12, jb);
    limb(grid, zbuf, cam, x, y + head - 0.40, z, x + swing * 0.5, y + hip * 0.7, z + 0.28,
         1, jr + 15, jg + 12, jb);
    limb(grid, zbuf, cam, x, y + hip, z, x + swing + tuck_x, y + foot_y, z - 0.30,
         2, 18, 100, 170);
    limb(grid, zbuf, cam, x, y + hip, z, x - swing + tuck_x, y + foot_y, z + 0.30,
         2, 14, 78, 145);
    int hc, hr;
    double hd;
    if (project(cam, x, y + head, z, &hc, &hr, &hd))
        stamp_disc(grid, zbuf, hr, hc, 3, 3, 0.01, ' ', 240, 200, 160);
}

static void stamp_text(Cell grid[][COLS], int row, const char *text, int R, int G,
                       int B) {
    int len = (int)strlen(text);
    int c0 = (COLS - len) / 2;
    if (row < 0 || row >= VROWS)
        return;
    for (int i = 0; i < len; i++) {
        int c = c0 + i;
        if (c < 0 || c >= COLS)
            continue;
        paint(&grid[row][c], ' ', 12, 8, 16, 12, 8, 16);
        grid[row][c].ch = text[i];
        grid[row][c].fr = (unsigned char)clampi(R, 0, 255);
        grid[row][c].fg = (unsigned char)clampi(G, 0, 255);
        grid[row][c].fb = (unsigned char)clampi(B, 0, 255);
    }
}

static void outline_pass(Cell grid[][COLS], double zbuf[][COLS]) {
    Cell copy[VROWS][COLS];
    memcpy(copy, grid, sizeof(copy));
    for (int r = 1; r < VROWS - 1; r++) {
        for (int c = 1; c < COLS - 1; c++) {
            double z = zbuf[r][c];
            if (z > 1.0e8)
                continue;
            int edge = 0;
            const double nb[4] = {zbuf[r][c - 1], zbuf[r][c + 1], zbuf[r - 1][c],
                                  zbuf[r + 1][c]};
            for (int k = 0; k < 4; k++) {
                if (nb[k] > z * 1.35 + 0.55)
                    edge = 1;
            }
            if (!edge)
                continue;
            copy[r][c].fr = (unsigned char)clampi(copy[r][c].fr + 36, 0, 255);
            copy[r][c].fg = (unsigned char)clampi(copy[r][c].fg + 36, 0, 255);
            copy[r][c].fb = (unsigned char)clampi(copy[r][c].fb + 28, 0, 255);
            copy[r][c].br = (unsigned char)(copy[r][c].br / 3);
            copy[r][c].bg = (unsigned char)(copy[r][c].bg / 3);
            copy[r][c].bb = (unsigned char)(copy[r][c].bb / 3);
        }
    }
    memcpy(grid, copy, sizeof(copy));
}

static int emit_frame(char *dst, size_t dst_n, Cell grid[][COLS]) {
    char *p = dst;
    char *end = dst + dst_n;
    int lr = -1, lg = -1, lb = -1, lbr = -1, lbg = -1, lbb = -1;
    /* Upper sample is foreground, lower sample is background, glyph is ▀. */
    static const char half[3] = {(char)0xE2, (char)0x96, (char)0x80};
    for (int r = 0; r < ROWS; r++) {
        lr = -1;
        for (int c = 0; c < COLS; c++) {
            const Cell *top = &grid[r * 2][c];
            const Cell *bot = &grid[r * 2 + 1][c];
            int fr = top->fr, fg = top->fg, fb = top->fb;
            int br = bot->br, bg = bot->bg, bb = bot->bb;
            char glyph[3];
            int glen = 3;
            glyph[0] = half[0];
            glyph[1] = half[1];
            glyph[2] = half[2];
            if (top->ch != ' ') {
                glyph[0] = top->ch;
                glen = 1;
                br = 12;
                bg = 8;
                bb = 16;
            }
            if (fr != lr || fg != lg || fb != lb || br != lbr || bg != lbg || bb != lbb) {
                char seq[64];
                int n = snprintf(seq, sizeof(seq),
                                 "\033[38;2;%d;%d;%d;48;2;%d;%d;%dm", fr, fg, fb, br, bg, bb);
                if (n < 0 || p + n >= end)
                    return -1;
                memcpy(p, seq, (size_t)n);
                p += n;
                lr = fr;
                lg = fg;
                lb = fb;
                lbr = br;
                lbg = bg;
                lbb = bb;
            }
            if (p + glen >= end)
                return -1;
            memcpy(p, glyph, (size_t)glen);
            p += glen;
        }
        if (p + 5 >= end)
            return -1;
        memcpy(p, "\033[0m\n", 5);
        p += 5;
    }
    if (p >= end)
        return -1;
    *p = '\0';
    return (int)(p - dst);
}

int parkour_render(const ParkourSnap *s, char *dst, size_t dst_n) {
    if (s == NULL || dst == NULL || dst_n < 64)
        return -1;
    Cam cam;
    cam_build(s, &cam);
    Cell grid[VROWS][COLS];
    double zbuf[VROWS][COLS];
    for (int r = 0; r < VROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            sky_at(r, &grid[r][c]);
            zbuf[r][c] = 1.0e9;
        }
    }
    for (int r = 0; r < VROWS; r++) {
        for (int c = 0; c < COLS; c++)
            trace_cell(s, &cam, c, r, &grid[r][c], &zbuf[r][c]);
    }
    outline_pass(grid, zbuf);
    stamp_runner(grid, zbuf, s, &cam);
    if (s->alive == 0 || s->state == 3) {
        stamp_text(grid, VROWS / 2 - 2, "DEAD", 255, 64, 64);
        stamp_text(grid, VROWS / 2 + 2, "r restart    q quit", 255, 220, 220);
    } else if (s->state == 2) {
        /* Visor band: the crouch reads even before the next obstacle. */
        for (int r = 0; r < 6; r++) {
            for (int c = 0; c < COLS; c++) {
                grid[r][c].fr = (unsigned char)(grid[r][c].fr / 5);
                grid[r][c].fg = (unsigned char)(grid[r][c].fg / 5);
                grid[r][c].fb = (unsigned char)(grid[r][c].fb / 5);
                grid[r][c].br = (unsigned char)(grid[r][c].br / 5);
                grid[r][c].bg = (unsigned char)(grid[r][c].bg / 5);
                grid[r][c].bb = (unsigned char)(grid[r][c].bb / 5);
            }
        }
    }

    int n = snprintf(dst, dst_n,
                     "\033[0m\033[1mFPS3D\033[0m score=%d %s tick=%d %s y=%.1f lane=%s\n",
                     s->score, s->alive ? "ALIVE" : "DEAD", s->tick, state_name(s->state),
                     s->y, lane_name(s->z));
    if (n < 0 || (size_t)n >= dst_n)
        return -1;
    int body = emit_frame(dst + n, dst_n - (size_t)n, grid);
    if (body < 0)
        return -1;
    return n + body;
}
