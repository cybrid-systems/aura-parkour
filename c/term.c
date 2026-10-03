#include "term.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>

static struct termios g_saved;
static int g_raw = 0;

int parkour_term_raw(int on) {
    if (!isatty(STDIN_FILENO))
        return -1;
    if (on && !g_raw) {
        struct termios t;
        if (tcgetattr(STDIN_FILENO, &g_saved) != 0)
            return -1;
        t = g_saved;
        t.c_lflag &= (tcflag_t)~(ICANON | ECHO);
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &t) != 0)
            return -1;
        g_raw = 1;
        return 0;
    }
    if (!on && g_raw) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
        g_raw = 0;
    }
    return 0;
}

void parkour_term_blit(const char *frame) {
    if (frame == NULL)
        return;
    if (isatty(STDOUT_FILENO))
        fputs("\033[2J\033[H", stdout);
    fputs(frame, stdout);
    if (frame[0] != '\0' && frame[strlen(frame) - 1] != '\n')
        fputc('\n', stdout);
    fflush(stdout);
}
