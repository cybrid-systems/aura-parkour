#ifndef PARKOUR_GLIDE_H
#define PARKOUR_GLIDE_H
#include "sample.h"

/* Display blend between two Soft snaps. Score, obstacles, and vx stay on `to`.
   A rewind or a step longer than 8 snaps instead of gliding. y follows Soft's
   Euler step so a jump has an apex between samples. */
void parkour_present(ParkourSnap *out, const ParkourSnap *from, const ParkourSnap *to,
                     double t);
#endif
