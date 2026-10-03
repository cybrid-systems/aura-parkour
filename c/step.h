#ifndef PARKOUR_STEP_H
#define PARKOUR_STEP_H
#include "sample.h"
/* One fixed step of the POD already in the snapshot, using Soft scalars.
   jump/slide are raw input bits. Does not append obstacles. */
void parkour_step(ParkourSnap *s, int jump, int slide);
#endif
