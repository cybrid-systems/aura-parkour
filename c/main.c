#include "clock.h"
#include "render.h"
#include "sample.h"
#include "step.h"
#include "term.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 3D viewport. Reads a Soft volume snapshot and rasters it.
   Does not generate chunks, corridors, or obstacle recipes.
   No complete SNAP -> exit 1, draw nothing.
   A truncated tail keeps the last accepted snapshot.
   --integrate steps existing AABBs with Soft scalars; it adds no boxes. */

static char *read_all(FILE *fp, size_t *out_n) {
    size_t cap = 4096, n = 0;
    char *buf = malloc(cap);
    if (buf == NULL)
        return NULL;
    for (;;) {
        if (n + 2048 > cap) {
            size_t ncap = cap * 2;
            char *grown = realloc(buf, ncap);
            if (grown == NULL) {
                free(buf);
                return NULL;
            }
            buf = grown;
            cap = ncap;
        }
        size_t got = fread(buf + n, 1, 2048, fp);
        n += got;
        if (got == 0)
            break;
    }
    if (n + 1 > cap) {
        char *grown = realloc(buf, n + 1);
        if (grown == NULL) {
            free(buf);
            return NULL;
        }
        buf = grown;
    }
    buf[n] = '\0';
    *out_n = n;
    return buf;
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [--integrate] [snapshot-file]\n"
            "       reads stdin when no file is given\n"
            "       blits one Soft snapshot; does not play a C parkour\n",
            argv0);
}

int main(int argc, char **argv) {
    const char *path = NULL;
    int integrate = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--integrate") == 0)
            integrate = 1;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            usage(argv[0]);
            return 2;
        } else if (path == NULL) {
            path = argv[i];
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    FILE *fp = stdin;
    if (path != NULL) {
        fp = fopen(path, "rb");
        if (fp == NULL) {
            fprintf(stderr, "parkour_blit: cannot open %s\n", path);
            return 1;
        }
    }
    size_t n = 0;
    char *text = read_all(fp, &n);
    if (path != NULL)
        fclose(fp);
    if (text == NULL) {
        fprintf(stderr, "parkour_blit: out of memory\n");
        return 1;
    }

    ParkourSnap snap;
    memset(&snap, 0, sizeof(snap));
    if (!parkour_sample_parse(text, n, &snap) || !snap.accepted) {
        fprintf(stderr, "parkour_blit: no accepted snapshot\n");
        free(text);
        return 1;
    }
    free(text);

    /* Touch the clock so the viewport has a monotonic source for a
       later fixed-dt loop. M0 does not sleep or invent a frame. */
    (void)parkour_clock_ns();

    if (integrate)
        parkour_step(&snap, 0, 0);

    char *frame = malloc(PARKOUR_FRAME_CAP);
    if (frame == NULL) {
        fprintf(stderr, "parkour_blit: out of memory\n");
        return 1;
    }
    if (parkour_render(&snap, frame, PARKOUR_FRAME_CAP) < 0) {
        fprintf(stderr, "parkour_blit: render failed\n");
        free(frame);
        return 1;
    }
    parkour_term_blit(frame);
    free(frame);
    return 0;
}
