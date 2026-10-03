#ifndef PARKOUR_RENDER_H
#define PARKOUR_RENDER_H
#include "sample.h"
#include <stddef.h>
/* First-person ANSI truecolor corridor. Soft AABBs only; no '@' map. */
#define PARKOUR_FRAME_CAP (256 * 1024)
int parkour_render(const ParkourSnap *s, char *dst, size_t dst_n);
#endif
