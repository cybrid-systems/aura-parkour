#ifndef PARKOUR_RENDER_H
#define PARKOUR_RENDER_H
#include "sample.h"
#include <stddef.h>
/* Perspective raster of snapshot boxes. Kind int -> glyph only. */
int parkour_render(const ParkourSnap *s, char *dst, size_t dst_n);
#endif
