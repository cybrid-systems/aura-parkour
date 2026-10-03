#include "sample.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void snap_clear(ParkourSnap *s) { memset(s, 0, sizeof(*s)); }

static int parse_one(const char *begin, const char *end, ParkourSnap *dst) {
    ParkourSnap tmp;
    snap_clear(&tmp);
    int saw_snap = 0, saw_scalar = 0, saw_player = 0, saw_obs = 0, saw_end = 0;
    int need = -1, got = 0;
    const char *p = begin;
    char line[512];
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        if (len >= sizeof(line))
            return 0;
        memcpy(line, p, len);
        line[len] = '\0';
        if (len > 0 && line[len - 1] == '\r')
            line[len - 1] = '\0';
        p = nl ? nl + 1 : end;
        if (line[0] == '\0' || line[0] == '#')
            continue;
        if (!saw_snap) {
            if (strcmp(line, "SNAP v1") != 0)
                return 0;
            saw_snap = 1;
            continue;
        }
        if (!saw_scalar) {
            if (sscanf(line, "SCALAR gravity=%lf jump_v=%lf slide_h=%lf",
                       &tmp.gravity, &tmp.jump_v, &tmp.slide_h) != 3)
                return 0;
            saw_scalar = 1;
            continue;
        }
        if (!saw_player) {
            if (sscanf(line,
                       "PLAYER x=%lf y=%lf z=%lf vx=%lf vy=%lf vz=%lf state=%d",
                       &tmp.x, &tmp.y, &tmp.z, &tmp.vx, &tmp.vy, &tmp.vz,
                       &tmp.state) != 7)
                return 0;
            saw_player = 1;
            tmp.alive = 1;
            tmp.score = 0;
            tmp.tick = 0;
            continue;
        }
        /* Optional Soft SCORE line (M1+). M0 snaps may omit it. */
        if (!saw_obs && strncmp(line, "SCORE ", 6) == 0) {
            int sc = 0, al = 1, tk = 0;
            if (sscanf(line, "SCORE score=%d alive=%d tick=%d", &sc, &al, &tk) != 3)
                return 0;
            tmp.score = sc;
            tmp.alive = al;
            tmp.tick = tk;
            continue;
        }
        if (!saw_obs) {
            if (sscanf(line, "OBS n=%d", &need) != 1 || need < 0 ||
                need > PARKOUR_MAX_OBS)
                return 0;
            saw_obs = 1;
            continue;
        }
        if (got < need) {
            ParkourObs *o = &tmp.obs[got];
            if (sscanf(line, "%d %d %d %d %d %d %d %d", &o->kind, &o->x, &o->y,
                       &o->z, &o->w, &o->h, &o->d, &o->flags) != 8)
                return 0;
            got++;
            continue;
        }
        if (strcmp(line, "END") == 0) {
            saw_end = 1;
            break;
        }
        return 0;
    }
    if (!saw_end || got != need)
        return 0;
    tmp.nobs = got;
    tmp.accepted = 1;
    *dst = tmp;
    return 1;
}

int parkour_sample_parse(const char *text, size_t n, ParkourSnap *out) {
    if (text == NULL || out == NULL)
        return 0;
    const char *p = text;
    const char *end = text + n;
    int any = out->accepted;
    while (p < end) {
        const char *hit = NULL;
        for (const char *q = p; q + 7 <= end; q++) {
            if (memcmp(q, "SNAP v1", 7) == 0 && (q == text || q[-1] == '\n')) {
                hit = q;
                break;
            }
        }
        if (hit == NULL)
            break;
        const char *next = NULL;
        for (const char *q = hit + 7; q + 7 <= end; q++) {
            if (memcmp(q, "SNAP v1", 7) == 0 && q[-1] == '\n') {
                next = q;
                break;
            }
        }
        ParkourSnap tmp = *out;
        if (parse_one(hit, next ? next : end, &tmp)) {
            *out = tmp;
            any = 1;
        }
        if (next == NULL)
            break;
        p = next;
    }
    return any;
}
