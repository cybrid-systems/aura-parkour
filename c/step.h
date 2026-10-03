#ifndef PARKOUR_STEP_H
#define PARKOUR_STEP_H
#include "sample.h"
/* Integrate the player through boxes already in the snapshot.
   Does not append volumes. jump/slide are raw input bits. */
void parkour_step(ParkourSnap *s, int jump, int slide);
#endif
