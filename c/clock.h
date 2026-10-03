#ifndef PARKOUR_CLOCK_H
#define PARKOUR_CLOCK_H
#include <stdint.h>
/* Monotonic nanoseconds. Not used to invent frames. */
uint64_t parkour_clock_ns(void);
#endif
