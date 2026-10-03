#include "render.h"

#include <stdio.h>

enum { COLS = 40, ROWS = 7 };

static char glyph_at(const ParkourSnap *s, int x, int y) {
    for (int i = 0; i < s->nobs; i++) {
        const ParkourObs *o = &s->obs[i];
        if (x < o->x || x >= o->x + o->w)
            continue;
        if (o->kind == 2 && y >= 0 && y < o->h)
            return '#';
        if (o->kind == 1 && y == 1)
            return '=';
        if (o->kind == 0 && y == 0)
            return 'v';
        if (o->kind == 4 && o->flags == 0 && y == 1)
            return 'o';
        if (o->kind == 3 && y == 0)
            return '+';
    }
    if (y == 0)
        return '.';
    return ' ';
}

int parkour_render(const ParkourSnap *s, char *dst, size_t dst_n) {
    if (s == NULL || dst == NULL || dst_n == 0)
        return -1;
    char grid[ROWS][COLS];
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++)
            grid[y][x] = glyph_at(s, x, y);
    }
    int px = (int)s->px;
    int py = (int)s->py;
    if (px >= 0 && px < COLS && py >= 0 && py < ROWS)
        grid[py][px] = '@';
    if (s->state != 2) {
        int hy = py + 1;
        if (px >= 0 && px < COLS && hy >= 0 && hy < ROWS)
            grid[hy][px] = '@';
    }
    size_t used = 0;
    int n = snprintf(dst, dst_n,
                     "SOFT gravity=%g jump_v=%g slide_h=%g state=%d obs=%d\n",
                     s->gravity, s->jump_v, s->slide_h, s->state, s->nobs);
    if (n < 0 || (size_t)n >= dst_n)
        return -1;
    used = (size_t)n;
    for (int y = ROWS - 1; y >= 0; y--) {
        if (used + (size_t)COLS + 1 >= dst_n)
            return -1;
        for (int x = 0; x < COLS; x++)
            dst[used++] = grid[y][x];
        dst[used++] = '\n';
    }
    dst[used] = '\0';
    return (int)used;
}
