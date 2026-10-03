#ifndef PARKOUR_RENDER_H
#define PARKOUR_RENDER_H
#include "sample.h"
#include <stddef.h>
/* Snapshot -> cells. Kind int maps to a glyph only. No level grammar. */
int parkour_render(const ParkourSnap *s, char *dst, size_t dst_n);
#endif
