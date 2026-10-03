#include "step.h"

static int overlap(const ParkourObs *o, int x) {
    return x >= o->x && x < o->x + o->w;
}

void parkour_step(ParkourSnap *s, int jump, int slide) {
    if (s == NULL || !s->accepted)
        return;
    if (s->state == 3)
        return;
    if (slide) {
        s->state = 2;
        s->vy = 0;
        s->py = 0;
    } else if (jump && s->py <= 0) {
        s->state = 1;
        s->vy = s->jump_v;
    } else if (s->py <= 0) {
        s->state = 0;
        s->vy = 0;
    }
    if (s->py > 0 || s->vy > 0) {
        s->vy -= s->gravity;
        s->py += s->vy;
        if (s->py < 0) {
            s->py = 0;
            s->vy = 0;
            if (s->state == 1)
                s->state = 0;
        }
    }
    s->px += s->vx;
    int x = (int)s->px;
    int y = (int)s->py;
    for (int i = 0; i < s->nobs; i++) {
        const ParkourObs *o = &s->obs[i];
        if (!overlap(o, x))
            continue;
        if (o->kind == 0 && y <= 0) {
            s->state = 3;
            return;
        }
        if (o->kind == 2 && y < o->h) {
            s->state = 3;
            return;
        }
        if (o->kind == 1 && s->state != 2 && y < 2) {
            s->state = 3;
            return;
        }
    }
}
