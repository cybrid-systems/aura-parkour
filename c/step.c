#include "step.h"

static int xz_hit(const ParkourObs *o, int x, int z) {
    return x >= o->x && x < o->x + o->w && z >= o->z && z < o->z + o->d;
}

static int vol_hit(const ParkourSnap *s, const ParkourObs *o, int x, int y, int z) {
    int ph = (s->state == 2) ? 1 : 2;
    return xz_hit(o, x, z) && y < o->y + o->h && (y + ph) > o->y;
}

void parkour_step(ParkourSnap *s, int jump, int slide) {
    if (s == NULL || !s->accepted || s->state == 3)
        return;
    if (slide) {
        s->state = 2;
        s->vy = 0;
        s->y = 0;
    } else if (jump && s->y <= 0) {
        s->state = 1;
        s->vy = s->jump_v;
    } else if (s->y <= 0) {
        s->state = 0;
        s->vy = 0;
    }
    if (s->y > 0 || s->vy > 0) {
        s->vy -= s->gravity;
        s->y += s->vy;
        if (s->y < 0) {
            s->y = 0;
            s->vy = 0;
            if (s->state == 1)
                s->state = 0;
        }
    }
    s->x += s->vx;
    s->z += s->vz;
    int x = (int)s->x, y = (int)s->y, z = (int)s->z;
    for (int i = 0; i < s->nobs; i++) {
        const ParkourObs *o = &s->obs[i];
        if (o->kind == 0 && xz_hit(o, x, z) && y <= 0) {
            s->state = 3;
            return;
        }
        if (o->kind == 2 && vol_hit(s, o, x, y, z)) {
            s->state = 3;
            return;
        }
        if (o->kind == 1 && s->state != 2 && vol_hit(s, o, x, y, z)) {
            s->state = 3;
            return;
        }
    }
}
