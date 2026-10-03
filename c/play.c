#define _POSIX_C_SOURCE 200809L
#include "clock.h"
#include "render.h"
#include "sample.h"
#include "term.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* Interactive 3D viewport. Soft owns world/score/death.
   This process only: poll keys, feed INPUT lines, blit SNAP frames.
   Soft is a child process (usually docker + aura play.aura). */

enum { FRAME_CAP = PARKOUR_FRAME_CAP, SNAP_CAP = 1 << 20 };


static void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) != 0) {
        if (errno != EINTR)
            break;
    }
}

static volatile sig_atomic_t g_stop = 0;
static void on_sig(int sig) {
    (void)sig;
    g_stop = 1;
}

static int write_all(int fd, const char *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, buf + off, n - off);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (w == 0)
            return -1;
        off += (size_t)w;
    }
    return 0;
}

static int read_snap(int fd, char *buf, size_t cap, size_t *out_n) {
    size_t n = 0;
    for (;;) {
        if (n + 1 >= cap)
            return -1;
        struct pollfd p = {.fd = fd, .events = POLLIN};
        int pr = poll(&p, 1, 120000);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (pr == 0)
            return -2; /* timeout waiting for Soft */
        ssize_t r = read(fd, buf + n, cap - 1 - n);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            return -3; /* Soft EOF */
        n += (size_t)r;
        buf[n] = '\0';
        /* Find last complete SNAP ... END\n */
        char *end = strstr(buf, "\nEND\n");
        if (end != NULL) {
            size_t complete = (size_t)(end - buf) + 5;
            /* Prefer last SNAP in buffer */
            char *snap = NULL;
            for (char *q = buf; q + 7 <= buf + complete; q++) {
                if (memcmp(q, "SNAP v1", 7) == 0 && (q == buf || q[-1] == '\n'))
                    snap = q;
            }
            if (snap != NULL) {
                size_t keep = (size_t)((buf + complete) - snap);
                memmove(buf, snap, keep);
                buf[keep] = '\0';
                *out_n = keep;
                return 0;
            }
        }
    }
}


enum { FRAME_MS = 33, GLIDE_MS = 96 };

static char *g_carry;
static size_t g_carry_n;

static int take_snap(char *dst, size_t cap, size_t *out_n) {
    if (g_carry == NULL || g_carry_n == 0)
        return 0;
    g_carry[g_carry_n] = '\0';
    char *end = strstr(g_carry, "\nEND\n");
    if (end == NULL)
        return 0;
    char *snap = NULL;
    for (char *q = g_carry; q + 7 <= end; q++) {
        if (memcmp(q, "SNAP v1", 7) == 0 && (q == g_carry || q[-1] == '\n'))
            snap = q;
    }
    if (snap == NULL)
        return 0;
    size_t keep = (size_t)(end + 5 - snap);
    if (keep + 1 > cap)
        return -1;
    memcpy(dst, snap, keep);
    dst[keep] = '\0';
    *out_n = keep;
    size_t rest = g_carry_n - (size_t)(end + 5 - g_carry);
    memmove(g_carry, end + 5, rest);
    g_carry_n = rest;
    g_carry[g_carry_n] = '\0';
    return 1;
}

/* 0 = one snap extracted, 1 = not ready yet, <0 = error. */
static int poll_snap(int fd, char *dst, size_t cap, size_t *out_n) {
    int got = take_snap(dst, cap, out_n);
    if (got != 0)
        return got > 0 ? 0 : got;
    struct pollfd p = {.fd = fd, .events = POLLIN};
    int pr = poll(&p, 1, 0);
    if (pr < 0) {
        if (errno == EINTR)
            return 1;
        return -1;
    }
    if (pr == 0)
        return 1;
    if (g_carry_n + 2 >= SNAP_CAP)
        return -1;
    ssize_t r = read(fd, g_carry + g_carry_n, SNAP_CAP - 1 - g_carry_n);
    if (r < 0) {
        if (errno == EINTR)
            return 1;
        return -1;
    }
    if (r == 0)
        return -3;
    g_carry_n += (size_t)r;
    g_carry[g_carry_n] = '\0';
    got = take_snap(dst, cap, out_n);
    if (got > 0)
        return 0;
    return got == 0 ? 1 : got;
}

static double smoothstep(double t) {
    if (t < 0.0)
        t = 0.0;
    if (t > 1.0)
        t = 1.0;
    return t * t * (3.0 - 2.0 * t);
}

static double cl01(double t) {
    if (t < 0.0)
        return 0.0;
    if (t > 1.0)
        return 1.0;
    return t;
}

/* Soft's step is vy := vy - g; y := y + vy. Pin both SNAP endpoints and
   bulge through the interior so the apex is more than one sample.
   A jump impulse is applied at the start of the tick, before that step,
   so a launch uses jump_v rather than the previous vy. */
static double ballistic_y(const ParkourSnap *a, const ParkourSnap *b, double u) {
    double g = b->gravity > 0.1 ? b->gravity : a->gravity;
    if (g < 0.1)
        g = 3.0;
    double vy = a->vy;
    if (a->y <= 0.05 && b->y > a->y + 0.2 && b->state == 1) {
        vy = b->jump_v > 0.1 ? b->jump_v : vy;
    }
    double end = a->y + (vy - g);
    if (end < 0.0)
        end = 0.0;
    double err = end - b->y;
    if (err < 0.0)
        err = -err;
    if (err > 0.35 || b->state == 2)
        return a->y + (b->y - a->y) * u;
    double y = a->y + (vy - g * u) * u;
    if (y < 0.0)
        y = 0.0;
    return y;
}

/* Display-only blend. Obstacles, score, and vx stay on the authoritative
   `to` snap. A rewind or a big jump (restart) snaps instead of gliding. */
static void present_snap(ParkourSnap *out, const ParkourSnap *a, const ParkourSnap *b,
                         double t) {
    *out = *b;
    double dx = b->x - a->x;
    if (dx < 0.0)
        dx = -dx;
    if (dx > 8.0 || b->tick < a->tick)
        return;
    double u = cl01(t);
    double s = smoothstep(u);
    out->x = a->x + (b->x - a->x) * u;
    out->z = a->z + (b->z - a->z) * s;
    out->y = ballistic_y(a, b, u);
    if (out->state != 2 && out->state != 3 && out->y > 0.15)
        out->state = 1;
}

static void pace_frame(uint64_t start_ns) {
    uint64_t now = parkour_clock_ns();
    uint64_t budget = (uint64_t)FRAME_MS * 1000000ull;
    if (now - start_ns >= budget)
        return;
    int ms = (int)((budget - (now - start_ns)) / 1000000ull);
    if (ms > 0)
        sleep_ms(ms);
}

static int spawn_soft(char *const argv[], int *in_fd, int *out_fd, pid_t *pid) {
    int to_soft[2], from_soft[2];
    if (pipe(to_soft) != 0 || pipe(from_soft) != 0)
        return -1;
    pid_t child = fork();
    if (child < 0)
        return -1;
    if (child == 0) {
        dup2(to_soft[0], STDIN_FILENO);
        dup2(from_soft[1], STDOUT_FILENO);
        close(to_soft[0]);
        close(to_soft[1]);
        close(from_soft[0]);
        close(from_soft[1]);
        /* Keep stderr for Soft seed diagnostics. */
        execvp(argv[0], argv);
        _exit(127);
    }
    close(to_soft[0]);
    close(from_soft[1]);
    *in_fd = to_soft[1];
    *out_fd = from_soft[0];
    *pid = child;
    return 0;
}

static int open_tty(void) {
    int fd = open("/dev/tty", O_RDWR);
    return fd;
}

static void drain_keys(int tty, int *jump, int *slide, int *dz, int *quit,
                       int *restart, int *pause_toggle) {
    *jump = 0;
    *slide = 0;
    *dz = 0;
    *quit = 0;
    *restart = 0;
    *pause_toggle = 0;
    if (tty < 0)
        return;
    for (;;) {
        struct pollfd p = {.fd = tty, .events = POLLIN};
        int pr = poll(&p, 1, 0);
        if (pr <= 0)
            break;
        unsigned char ch = 0;
        if (read(tty, &ch, 1) != 1)
            break;
        switch (ch) {
        case ' ':
        case 'w':
        case 'W':
            *jump = 1;
            break;
        case 's':
        case 'S':
            *slide = 1;
            break;
        case 'a':
        case 'A':
            *dz = -1;
            break;
        case 'd':
        case 'D':
            *dz = 1;
            break;
        case 'p':
        case 'P':
            *pause_toggle = 1;
            break;
        case 'q':
        case 'Q':
            *quit = 1;
            break;
        case 'r':
        case 'R':
            *restart = 1;
            break;
        default:
            break;
        }
    }
}

static void blit_hud(int tty, const ParkourSnap *s, const char *frame, int paused) {
    char extra[256];
    snprintf(extra, sizeof(extra),
             "%s | Space/w jump  s slide  a/d lane  p pause  r restart  q quit\n",
             paused ? "PAUSED" : (s->alive ? "RUN" : "DEAD - r restart / q quit"));
    if (tty >= 0) {
        if (isatty(tty))
            write_all(tty, "\033[2J\033[H", 6);
        write_all(tty, frame, strlen(frame));
        write_all(tty, extra, strlen(extra));
    } else {
        parkour_term_blit(frame);
        fputs(extra, stdout);
        fflush(stdout);
    }
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s -- soft-command [args...]\n"
            "       Soft child speaks SNAP on stdout; accepts INPUT on stdin.\n",
            argv0);
}

int main(int argc, char **argv) {
    int soft_i = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            soft_i = i + 1;
            break;
        }
    }
    if (soft_i < 0 || soft_i >= argc) {
        usage(argv[0]);
        return 2;
    }

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    int soft_in = -1, soft_out = -1;
    pid_t soft_pid = -1;
    if (spawn_soft(&argv[soft_i], &soft_in, &soft_out, &soft_pid) != 0) {
        fprintf(stderr, "parkour_play: cannot spawn Soft child\n");
        return 1;
    }

    int tty = open_tty();
    if (tty >= 0)
        parkour_term_raw(1); /* still try stdin; also configure via tc on tty below */
    /* Raw on /dev/tty explicitly */
    struct termios saved, raw;
    int raw_on = 0;
    if (tty >= 0 && tcgetattr(tty, &saved) == 0) {
        raw = saved;
        raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(tty, TCSANOW, &raw) == 0)
            raw_on = 1;
    }

    char *snapbuf = malloc(SNAP_CAP);
    char *frame = malloc(FRAME_CAP);
    if (snapbuf == NULL || frame == NULL) {
        fprintf(stderr, "parkour_play: oom\n");
        free(snapbuf);
        free(frame);
        return 1;
    }

    fprintf(stderr, "parkour_play: waiting for Soft world (first seed can take ~30-60s)...\n");

    ParkourSnap snap;
    memset(&snap, 0, sizeof(snap));
    size_t snap_n = 0;
    if (read_snap(soft_out, snapbuf, SNAP_CAP, &snap_n) != 0 ||
        !parkour_sample_parse(snapbuf, snap_n, &snap) || !snap.accepted) {
        fprintf(stderr, "parkour_play: Soft did not produce an initial SNAP\n");
        g_stop = 1;
    } else {
        if (parkour_render(&snap, frame, FRAME_CAP) >= 0)
            blit_hud(tty, &snap, frame, 0);
    }

    g_carry = malloc(SNAP_CAP);
    if (g_carry == NULL) {
        fprintf(stderr, "parkour_play: oom\n");
        free(snapbuf);
        free(frame);
        return 1;
    }
    g_carry_n = 0;

    ParkourSnap from = snap;
    ParkourSnap to = snap;
    ParkourSnap view;
    double alpha = 1.0;
    int coin_burst = 0;
    int awaiting = 0;
    int latch_jump = 0, latch_slide = 0, latch_dz = 0, latch_restart = 0;
    int paused = 0;
    while (!g_stop) {
        uint64_t frame_t = parkour_clock_ns();
        int jump = 0, slide = 0, dz = 0, quit = 0, restart = 0, pt = 0;
        drain_keys(tty >= 0 ? tty : STDIN_FILENO, &jump, &slide, &dz, &quit,
                   &restart, &pt);
        if (pt)
            paused = !paused;
        if (jump)
            latch_jump = 1;
        if (slide)
            latch_slide = 1;
        if (dz)
            latch_dz = dz;
        if (restart)
            latch_restart = 1;
        if (quit) {
            char line[64];
            int n = snprintf(line, sizeof(line), "INPUT 0 0 0 1 0\n");
            write_all(soft_in, line, (size_t)n);
            break;
        }

        if ((paused && !latch_restart) ||
            (to.accepted && to.alive == 0 && !latch_restart)) {
            present_snap(&view, &from, &to, alpha);
            view.fx_coin = coin_burst;
            if (parkour_render(&view, frame, FRAME_CAP) >= 0)
                blit_hud(tty, &to, frame, paused);
            pace_frame(frame_t);
            continue;
        }

        /* Advance the glide first so a snap that lands this frame is shown at t=0. */
        if (!awaiting && alpha < 1.0) {
            alpha += (double)FRAME_MS / (double)GLIDE_MS;
            if (alpha > 1.0)
                alpha = 1.0;
        }
        /* Glide across the last authoritative step, then ask Soft for the next. */
        if (!awaiting && alpha >= 1.0) {
            char line[80];
            int n = snprintf(line, sizeof(line), "INPUT %d %d %d 0 %d\n", latch_jump,
                             latch_slide, latch_dz, latch_restart);
            latch_jump = latch_slide = latch_dz = latch_restart = 0;
            if (write_all(soft_in, line, (size_t)n) != 0) {
                fprintf(stderr, "parkour_play: Soft stdin closed\n");
                break;
            }
            awaiting = 1;
        }
        if (awaiting) {
            int rs = poll_snap(soft_out, snapbuf, SNAP_CAP, &snap_n);
            if (rs == 0) {
                ParkourSnap next;
                memset(&next, 0, sizeof(next));
                if (!parkour_sample_parse(snapbuf, snap_n, &next) || !next.accepted) {
                    fprintf(stderr, "parkour_play: Soft SNAP parse failed\n");
                    break;
                }
                from = to;
                to = next;
                snap = next;
                alpha = 0.0;
                awaiting = 0;
                if (to.score - from.score >= 5)
                    coin_burst = 8;
            } else if (rs < 0) {
                fprintf(stderr, "parkour_play: Soft SNAP read failed (%d)\n", rs);
                break;
            }
        }
        present_snap(&view, &from, &to, awaiting ? 1.0 : alpha);
        view.fx_coin = coin_burst;
        if (coin_burst > 0)
            coin_burst--;
        if (parkour_render(&view, frame, FRAME_CAP) < 0) {
            fprintf(stderr, "parkour_play: render failed\n");
            break;
        }
        blit_hud(tty, &to, frame, 0);
        pace_frame(frame_t);
    }

    if (raw_on)
        tcsetattr(tty, TCSANOW, &saved);
    parkour_term_raw(0);
    if (tty >= 0)
        close(tty);
    close(soft_in);
    close(soft_out);
    if (soft_pid > 0) {
        kill(soft_pid, SIGTERM);
        waitpid(soft_pid, NULL, 0);
    }
    free(snapbuf);
    free(frame);
    free(g_carry);
    return 0;
}
