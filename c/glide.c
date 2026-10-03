#include "glide.h"

static double cl01(double t) {
    if (t < 0.0)
        return 0.0;
    if (t > 1.0)
        return 1.0;
    return t;
}

static double smoothstep(double t) {
    t = cl01(t);
    return t * t * (3.0 - 2.0 * t);
}

/* Soft's step is vy := vy - g; y := y + vy. Pin both endpoints and bulge
   the interior. A jump impulse lands at the start of the tick, so a launch
   uses jump_v rather than the previous vy. */
static double ballistic_y(const ParkourSnap *a, const ParkourSnap *b, double u) {
    double g = b->gravity > 0.1 ? b->gravity : a->gravity;
    if (g < 0.1)
        g = 3.0;
    double vy = a->vy;
    if (a->y <= 0.05 && b->y > a->y + 0.2 && b->state == 1) {
        vy = b->jump_v > 0.1 ? b->jump_v : vy;
    }
    double end = a->y + (vy - g);
    if (end < 0.0)
        end = 0.0;
    double err = end - b->y;
    if (err < 0.0)
        err = -err;
    if (err > 0.35 || b->state == 2)
        return a->y + (b->y - a->y) * u;
    double y = a->y + (vy - g * u) * u;
    if (y < 0.0)
        y = 0.0;
    return y;
}

void parkour_present(ParkourSnap *out, const ParkourSnap *a, const ParkourSnap *b, double t) {
    *out = *b;
    double dx = b->x - a->x;
    if (dx < 0.0)
        dx = -dx;
    if (dx > 8.0 || b->tick < a->tick)
        return;
    double u = cl01(t);
    double s = smoothstep(u);
    out->x = a->x + (b->x - a->x) * u;
    out->z = a->z + (b->z - a->z) * s;
    out->y = ballistic_y(a, b, u);
    if (out->state != 2 && out->state != 3 && out->y > 0.15)
        out->state = 1;
}
