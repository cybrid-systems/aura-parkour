#define _POSIX_C_SOURCE 200809L
#include "clock.h"
#include <time.h>

uint64_t parkour_clock_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
