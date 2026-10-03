#define _POSIX_C_SOURCE 200809L
/* Soft-fed 3D corridor. Soft owns the world; this process only draws and
   sends INPUT. Glide is parkour_present(), the same blend as the ANSI view. */
#include "clock.h"
#include "glide.h"
#include "sample.h"

#include <errno.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define RAYMATH_IMPLEMENTATION
#include <raylib.h>
#include <raymath.h>

enum { SNAP_CAP = 1 << 20 };

static const char *VS = "#version 330\n"
                         "in vec3 vertexPosition;\n"
                         "in vec2 vertexTexCoord;\n"
                         "in vec3 vertexNormal;\n"
                         "in vec4 vertexColor;\n"
                         "uniform mat4 mvp;\n"
                         "uniform mat4 matModel;\n"
                         "uniform mat4 matNormal;\n"
                         "out vec3 fragPosition;\n"
                         "out vec3 fragNormal;\n"
                         "out vec4 fragColor;\n"
                         "void main() {\n"
                         "  fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));\n"
                         "  fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 1.0)));\n"
                         "  fragColor = vertexColor;\n"
                         "  gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
                         "}\n";

static const char *FS = "#version 330\n"
                         "in vec3 fragPosition;\n"
                         "in vec3 fragNormal;\n"
                         "in vec4 fragColor;\n"
                         "uniform vec4 colDiffuse;\n"
                         "uniform vec3 viewPos;\n"
                         "out vec4 finalColor;\n"
                         "void main() {\n"
                         "  vec3 n = normalize(fragNormal);\n"
                         "  vec3 toLight = normalize(vec3(-0.35, 0.9, 0.2));\n"
                         "  float diff = max(dot(n, toLight), 0.0);\n"
                         "  float amb = 0.34;\n"
                         "  vec3 base = colDiffuse.rgb * fragColor.rgb;\n"
                         "  vec3 lit = base * (amb + diff * 0.9);\n"
                         "  float dist = length(viewPos - fragPosition);\n"
                         "  float fog = clamp((dist - 6.0) / 34.0, 0.0, 1.0);\n"
                         "  vec3 fogCol = vec3(0.05, 0.07, 0.13);\n"
                         "  lit = mix(lit, fogCol, fog);\n"
                         "  finalColor = vec4(lit, colDiffuse.a);\n"
                         "}\n";

static volatile sig_atomic_t g_stop = 0;
static void on_sig(int sig) {
    (void)sig;
    g_stop = 1;
}

static char *g_carry;
static size_t g_carry_n;
static int g_smoke = 0;
static int g_smoke_step = 0;

static Mesh g_cube;
static Mesh g_sphere;
static Material g_mat;
static Shader g_shader;
static int g_lit = 0;
static int g_view_loc = -1;

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

/* 0 = one snap, 1 = not yet, <0 = error. */
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

static void draw_mesh(Vector3 center, Vector3 size, Color color) {
    g_mat.maps[MATERIAL_MAP_DIFFUSE].color = color;
    Matrix xform = MatrixMultiply(MatrixTranslate(center.x, center.y, center.z),
                                  MatrixScale(size.x, size.y, size.z));
    if (g_lit)
        DrawMesh(g_cube, g_mat, xform);
    else
        DrawCube(center, size.x, size.y, size.z, color);
}

static void draw_sphere(Vector3 center, float radius, Color color) {
    g_mat.maps[MATERIAL_MAP_DIFFUSE].color = color;
    Matrix xform = MatrixMultiply(MatrixTranslate(center.x, center.y, center.z),
                                  MatrixScale(radius, radius, radius));
    if (g_lit)
        DrawMesh(g_sphere, g_mat, xform);
    else
        DrawSphere(center, radius, color);
}

static int gap_here(const ParkourSnap *s, float x, float z) {
    for (int i = 0; i < s->nobs; i++) {
        const ParkourObs *o = &s->obs[i];
        if (o->kind != 0)
            continue;
        if (x >= (float)o->x && x < (float)(o->x + o->w) && z >= (float)o->z &&
            z < (float)(o->z + o->d))
            return 1;
    }
    return 0;
}

static void draw_corridor(const ParkourSnap *s) {
    int x0 = (int)s->x - 4;
    int x1 = (int)s->x + 42;
    for (int x = x0; x < x1; x += 2) {
        for (int lane = -2; lane <= 2; lane += 2) {
            float cx = (float)x + 1.0f;
            float cz = (float)lane;
            if (gap_here(s, cx, cz))
                continue;
            int checker = ((x / 2) + (lane / 2)) & 1;
            Color c = checker ? (Color){78, 94, 112, 255} : (Color){46, 56, 72, 255};
            draw_mesh((Vector3){cx, -0.08f, cz}, (Vector3){2.0f, 0.16f, 1.85f}, c);
        }
        Color wall = {36, 44, 66, 255};
        Color trim = {88, 108, 146, 255};
        float wx = (float)x + 1.0f;
        draw_mesh((Vector3){wx, 2.1f, -4.55f}, (Vector3){2.0f, 4.2f, 0.35f}, wall);
        draw_mesh((Vector3){wx, 2.1f, 4.55f}, (Vector3){2.0f, 4.2f, 0.35f}, wall);
        draw_mesh((Vector3){wx, 4.05f, -4.55f}, (Vector3){2.0f, 0.18f, 0.42f}, trim);
        draw_mesh((Vector3){wx, 4.05f, 4.55f}, (Vector3){2.0f, 0.18f, 0.42f}, trim);
    }
    for (int i = 0; i < s->nobs; i++) {
        const ParkourObs *o = &s->obs[i];
        if (o->kind == 0)
            continue;
        if (o->kind == 4 && (o->flags & 1))
            continue;
        float cx = (float)o->x + (float)o->w * 0.5f;
        float cy = (float)o->y + (float)o->h * 0.5f;
        float cz = (float)o->z + (float)o->d * 0.5f;
        if (cx < s->x - 6.0 || cx > s->x + 46.0)
            continue;
        if (o->kind == 4) {
            float spin = (float)GetTime() * 2.4f + cx;
            g_mat.maps[MATERIAL_MAP_DIFFUSE].color = (Color){255, 214, 48, 255};
            Matrix xform = MatrixMultiply(
                MatrixTranslate(cx, cy, cz),
                MatrixMultiply(MatrixRotateY(spin), MatrixScale(0.62f, 0.14f, 0.62f)));
            if (g_lit)
                DrawMesh(g_cube, g_mat, xform);
            else
                DrawCylinder((Vector3){cx, cy - 0.08f, cz}, 0.38f, 0.38f, 0.16f, 12,
                             (Color){255, 214, 48, 255});
            continue;
        }
        Color c = {180, 186, 200, 255};
        if (o->kind == 1)
            c = (Color){232, 176, 42, 255};
        else if (o->kind == 2)
            c = (Color){204, 72, 46, 255};
        else if (o->kind == 3)
            c = (Color){32, 186, 146, 255};
        draw_mesh((Vector3){cx, cy, cz},
                  (Vector3){(float)o->w, (float)o->h, (float)o->d}, c);
        DrawCubeWires((Vector3){cx, cy, cz}, (float)o->w + 0.02f, (float)o->h + 0.02f,
                      (float)o->d + 0.02f, (Color){255, 244, 220, 180});
    }
}

static void limb(Vector3 a, Vector3 b, float thick, Color color) {
    Vector3 mid = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f};
    float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len < 0.05f)
        len = 0.05f;
    /* Cubes stay axis-aligned; the long axis follows the bigger delta. */
    Vector3 size = {thick, thick, thick};
    if (fabsf(dx) >= fabsf(dy) && fabsf(dx) >= fabsf(dz))
        size.x = len;
    else if (fabsf(dy) >= fabsf(dz))
        size.y = len;
    else
        size.z = len;
    draw_mesh(mid, size, color);
}

static void draw_runner(const ParkourSnap *s) {
    float x = (float)s->x + 0.15f;
    float y = (float)s->y;
    float z = (float)s->z;
    float swing = 0.0f;
    float hip = 0.95f;
    float head = 1.62f;
    float foot = 0.08f;
    float tuck = 0.0f;
    if (s->state == 2) {
        hip = 0.38f;
        head = 0.62f;
        foot = 0.08f;
        tuck = 0.45f;
    } else if (s->state == 1 || s->y > 0.25) {
        hip = 0.85f;
        head = 1.48f;
        foot = 0.48f;
        tuck = 0.22f;
        swing = 0.18f;
    } else {
        swing = sinf((float)s->x * 3.3f) * 0.42f;
    }
    Color shirt = s->state == 3 ? (Color){170, 40, 40, 255} : (Color){28, 150, 220, 255};
    Color skin = {236, 190, 154, 255};
    Color leg = {16, 36, 78, 255};
    draw_mesh((Vector3){x, y + (hip + head) * 0.5f, z},
              (Vector3){0.46f, head - hip, 0.34f}, shirt);
    limb((Vector3){x, y + hip, z}, (Vector3){x + swing + tuck, y + foot, z - 0.22f}, 0.16f, leg);
    limb((Vector3){x, y + hip, z}, (Vector3){x - swing + tuck, y + foot, z + 0.22f}, 0.16f, leg);
    limb((Vector3){x, y + head - 0.35f, z},
         (Vector3){x - swing * 0.8f, y + hip * 0.55f, z - 0.48f}, 0.12f, skin);
    limb((Vector3){x, y + head - 0.35f, z},
         (Vector3){x + swing * 0.8f, y + hip * 0.55f, z + 0.48f}, 0.12f, skin);
    draw_sphere((Vector3){x, y + head, z}, 0.22f, skin);
}

static double iv_gap(double a0, double a1, double b0, double b1) {
    if (a1 < b0)
        return b0 - a1;
    if (b1 < a0)
        return a0 - b1;
    return 0.0;
}

static int near_miss(const ParkourSnap *s) {
    if (s->alive == 0 || s->state == 3)
        return 0;
    double body = s->state == 2 ? 1.0 : 2.0;
    for (int i = 0; i < s->nobs; i++) {
        const ParkourObs *o = &s->obs[i];
        if (o->kind == 4)
            continue;
        double x0 = (double)o->x, x1 = x0 + (double)o->w;
        double y0 = (double)o->y, y1 = y0 + (double)o->h;
        double z0 = (double)o->z, z1 = z0 + (double)o->d;
        double xg = iv_gap(s->x, s->x + 0.2, x0, x1);
        if (xg > 0.8)
            continue;
        if (o->kind == 0) {
            if (xg == 0.0 && s->y > 0.05 && s->y < 1.05)
                return 1;
            continue;
        }
        double yg = iv_gap(s->y, s->y + body, y0, y1);
        double zg = iv_gap(s->z, s->z + 0.2, z0, z1);
        if (xg == 0.0 && yg == 0.0 && zg == 0.0)
            continue;
        if (xg == 0.0 && zg == 0.0 && yg > 0.0 && yg <= 1.05)
            return 1;
        if (xg == 0.0 && yg == 0.0 && zg > 0.0 && zg <= 1.15)
            return 1;
    }
    return 0;
}

static void draw_burst(const ParkourSnap *s, int frames) {
    if (frames <= 0)
        return;
    float k = (float)frames / 18.0f;
    for (int i = 0; i < 8; i++) {
        float ang = (float)i / 8.0f * 6.28318f + (float)GetTime();
        float spread = (1.0f - k) * 1.4f + 0.3f;
        Vector3 p = {(float)s->x + 0.4f + cosf(ang) * spread * 0.2f,
                     (float)s->y + 1.2f + sinf(ang) * spread, (float)s->z + cosf(ang * 2.0f) * spread};
        draw_sphere(p, 0.12f + k * 0.08f, (Color){255, 220, 60, 255});
    }
}

static void draw_hud(const ParkourSnap *s, int paused, int miss, int burst) {
    int w = GetScreenWidth();
    int h = GetScreenHeight();
    const char *state = "RUN";
    if (s->state == 1)
        state = "JUMP";
    else if (s->state == 2)
        state = "SLIDE";
    else if (!s->alive || s->state == 3)
        state = "DEAD";
    if (paused)
        state = "PAUSED";
    DrawText(TextFormat("score %d   spd %.0f   %s   y %.1f", s->score, s->vx, state, s->y), 16,
             14, 22, (Color){236, 240, 248, 255});
    DrawText("space/w jump   s slide   a/d lane   p pause   r restart   q quit", 16, h - 28, 16,
             (Color){180, 190, 210, 220});
    if (miss) {
        DrawRectangle(0, 0, w, 28, (Color){255, 176, 40, 160});
        DrawRectangle(0, h - 28, w, 28, (Color){255, 176, 40, 160});
    }
    if (burst > 0)
        DrawRectangleLinesEx((Rectangle){8, 8, (float)w - 16, (float)h - 16}, 3.0f,
                             (Color){255, 210, 50, 180});
    if (!s->alive || s->state == 3) {
        DrawRectangle(0, 0, w, h, (Color){40, 0, 0, 90});
        DrawText("DEAD", w / 2 - 70, h / 2 - 30, 60, (Color){255, 80, 80, 255});
        DrawText("r restart    q quit", w / 2 - 110, h / 2 + 40, 24, RAYWHITE);
    }
}

static Camera3D chase_cam(const ParkourSnap *s) {
    float bob = 0.0f;
    if (s->state == 0)
        bob = sinf((float)s->x * 2.4f) * 0.07f;
    float eye_y = (float)s->y + (s->state == 2 ? 1.15f : 2.45f) + bob;
    float fov = 58.0f + (float)s->vx * 2.5f;
    if (fov > 74.0f)
        fov = 74.0f;
    Camera3D cam = {0};
    cam.position = (Vector3){(float)s->x - 6.2f, eye_y, (float)s->z};
    cam.target = (Vector3){(float)s->x + 8.0f, (float)s->y + 1.15f, (float)s->z};
    cam.up = (Vector3){0.0f, 1.0f, 0.0f};
    cam.fovy = fov;
    cam.projection = CAMERA_PERSPECTIVE;
    return cam;
}

static void paint(const ParkourSnap *s, int paused, int burst) {
    if (g_lit && g_view_loc >= 0) {
        Camera3D cam = chase_cam(s);
        float view[3] = {cam.position.x, cam.position.y, cam.position.z};
        SetShaderValue(g_shader, g_view_loc, view, SHADER_UNIFORM_VEC3);
    }
    BeginDrawing();
    ClearBackground((Color){10, 14, 26, 255});
    BeginMode3D(chase_cam(s));
    if (g_lit)
        BeginShaderMode(g_shader);
    draw_corridor(s);
    draw_runner(s);
    draw_burst(s, burst);
    if (g_lit)
        EndShaderMode();
    EndMode3D();
    draw_hud(s, paused, near_miss(s), burst);
    DrawFPS(GetScreenWidth() - 90, 14);
    EndDrawing();
}

static void latch_keys(int *jump, int *slide, int *dz, int *quit, int *restart, int *pause) {
    if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_W))
        *jump = 1;
    if (IsKeyPressed(KEY_S))
        *slide = 1;
    if (IsKeyPressed(KEY_A))
        *dz = -1;
    if (IsKeyPressed(KEY_D))
        *dz = 1;
    if (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_ESCAPE))
        *quit = 1;
    if (IsKeyPressed(KEY_R))
        *restart = 1;
    if (IsKeyPressed(KEY_P))
        *pause = 1;
}

static void smoke_latch(int *jump, int *slide, int *dz, int *quit) {
    if (g_smoke_step == 0)
        *jump = 1;
    else if (g_smoke_step == 1)
        *slide = 1;
    else if (g_smoke_step == 2)
        *dz = 1;
    else if (g_smoke_step >= 5)
        *quit = 1;
    g_smoke_step++;
}

static void log_snap(const ParkourSnap *s) {
    fprintf(stderr, "VIEW3D x=%.2f y=%.2f z=%.2f vx=%.0f state=%d score=%d alive=%d tick=%d\n",
            s->x, s->y, s->z, s->vx, s->state, s->score, s->alive, s->tick);
}


/* No Cocoa display in this process. Still prove the Soft pipe: scripted
   jump, slide, lane, two coasts, quit. */
static int smoke_headless(int soft_in, int soft_out, char *snapbuf) {
    fprintf(stderr, "parkour_view: headless smoke (no display)\n");
    ParkourSnap cur;
    memset(&cur, 0, sizeof(cur));
    uint64_t start = parkour_clock_ns();
    int got_boot = 0;
    while (!got_boot) {
        size_t n = 0;
        int rs = poll_snap(soft_out, snapbuf, SNAP_CAP, &n);
        if (rs == 0) {
            if (!parkour_sample_parse(snapbuf, n, &cur) || !cur.accepted)
                return 1;
            log_snap(&cur);
            got_boot = 1;
            break;
        }
        if (rs < 0)
            return 1;
        if (parkour_clock_ns() - start > 90ull * 1000000000ull)
            return 1;
        struct timespec ts = {0, 50 * 1000000L};
        nanosleep(&ts, NULL);
    }
    for (int step = 0; step < 6; step++) {
        int jump = 0, slide = 0, dz = 0, quit = 0;
        g_smoke_step = step;
        smoke_latch(&jump, &slide, &dz, &quit);
        char line[80];
        int n = snprintf(line, sizeof(line), "INPUT %d %d %d %d 0\n", jump, slide, dz, quit);
        fprintf(stderr, "VIEW3D sent %s", line);
        if (write_all(soft_in, line, (size_t)n) != 0)
            return 1;
        if (quit)
            break;
        uint64_t t0 = parkour_clock_ns();
        int got = 0;
        while (!got) {
            size_t sn = 0;
            int rs = poll_snap(soft_out, snapbuf, SNAP_CAP, &sn);
            if (rs == 0) {
                ParkourSnap next;
                memset(&next, 0, sizeof(next));
                if (!parkour_sample_parse(snapbuf, sn, &next) || !next.accepted)
                    return 1;
                /* Display blend must move y off the straight chord on a launch. */
                if (step == 0) {
                    ParkourSnap mid;
                    parkour_present(&mid, &cur, &next, 0.5);
                    fprintf(stderr, "VIEW3D arc y0=%.2f ymid=%.2f y1=%.2f\n", cur.y, mid.y, next.y);
                }
                cur = next;
                log_snap(&cur);
                got = 1;
                break;
            }
            if (rs < 0)
                return 1;
            if (parkour_clock_ns() - t0 > 60ull * 1000000000ull)
                return 1;
            struct timespec ts = {0, 50 * 1000000L};
            nanosleep(&ts, NULL);
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    int soft_i = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            soft_i = i + 1;
            break;
        }
        if (strcmp(argv[i], "--smoke") == 0)
            g_smoke = 1;
    }
    if (soft_i < 0 || soft_i >= argc) {
        fprintf(stderr, "usage: %s [--smoke] -- soft-command [args...]\n", argv[0]);
        return 2;
    }
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    int soft_in = -1, soft_out = -1;
    pid_t soft_pid = -1;
    if (spawn_soft(&argv[soft_i], &soft_in, &soft_out, &soft_pid) != 0) {
        fprintf(stderr, "parkour_view: failed to spawn Soft\n");
        return 1;
    }
    g_carry = calloc(1, SNAP_CAP);
    char *snapbuf = calloc(1, SNAP_CAP);
    if (g_carry == NULL || snapbuf == NULL)
        return 1;

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(1280, 720, "aura-parkour");
    if (!IsWindowReady()) {
        fprintf(stderr, "parkour_view: no display (run from a Mac GUI session, or use the ANSI fallback)\n");
        int rc = 1;
        if (g_smoke)
            rc = smoke_headless(soft_in, soft_out, snapbuf);
        kill(soft_pid, SIGTERM);
        for (int i = 0; i < 20; i++) {
            if (waitpid(soft_pid, NULL, WNOHANG) == soft_pid)
                break;
            struct timespec ts = {0, 50 * 1000000L};
            nanosleep(&ts, NULL);
        }
        kill(soft_pid, SIGKILL);
        waitpid(soft_pid, NULL, 0);
        close(soft_in);
        close(soft_out);
        free(snapbuf);
        free(g_carry);
        return rc;
    }
    SetTargetFPS(60);
    g_shader = LoadShaderFromMemory(VS, FS);
    g_lit = g_shader.id != 0;
    if (g_lit) {
        g_shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(g_shader, "mvp");
        g_shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(g_shader, "matModel");
        g_shader.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(g_shader, "matNormal");
        g_shader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(g_shader, "colDiffuse");
        g_view_loc = GetShaderLocation(g_shader, "viewPos");
        g_shader.locs[SHADER_LOC_VECTOR_VIEW] = g_view_loc;
    }
    g_cube = GenMeshCube(1.0f, 1.0f, 1.0f);
    g_sphere = GenMeshSphere(1.0f, 16, 12);
    g_mat = LoadMaterialDefault();
    if (g_lit)
        g_mat.shader = g_shader;

    ParkourSnap from, to, view;
    memset(&from, 0, sizeof(from));
    memset(&to, 0, sizeof(to));
    int have = 0;
    uint64_t wait_start = parkour_clock_ns();
    while (!WindowShouldClose() && !g_stop && !have) {
        size_t n = 0;
        int rs = poll_snap(soft_out, snapbuf, SNAP_CAP, &n);
        if (rs == 0) {
            if (parkour_sample_parse(snapbuf, n, &to) && to.accepted) {
                from = to;
                have = 1;
                log_snap(&to);
                break;
            }
        } else if (rs < 0) {
            fprintf(stderr, "parkour_view: Soft SNAP read failed (%d)\n", rs);
            break;
        }
        if ((parkour_clock_ns() - wait_start) > 120ull * 1000000000ull) {
            fprintf(stderr, "parkour_view: timed out waiting for Soft\n");
            break;
        }
        BeginDrawing();
        ClearBackground((Color){10, 14, 26, 255});
        DrawText("seeding Soft world...", 48, 80, 32, RAYWHITE);
        DrawText("first seed can take about a minute", 48, 130, 20, (Color){180, 190, 210, 255});
        EndDrawing();
    }
    if (!have) {
        CloseWindow();
        return 1;
    }

    float alpha = 1.0f;
    int awaiting = 0;
    int latch_jump = 0, latch_slide = 0, latch_dz = 0, latch_restart = 0, paused = 0;
    int coin_burst = 0;
    int quit = 0;
    while (!WindowShouldClose() && !g_stop && !quit) {
        int jump = 0, slide = 0, dz = 0, restart = 0, pt = 0, q = 0;
        if (!g_smoke)
            latch_keys(&jump, &slide, &dz, &q, &restart, &pt);
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
        if (q)
            quit = 1;

        int frozen = (paused && !latch_restart) || (to.alive == 0 && !latch_restart);
        if (!frozen) {
            float dt = GetFrameTime();
            if (dt < 0.001f)
                dt = 1.0f / 60.0f;
            if (!awaiting && alpha < 1.0f) {
                alpha += dt / 0.096f;
                if (alpha > 1.0f)
                    alpha = 1.0f;
            }
            if (!awaiting && alpha >= 1.0f) {
                if (g_smoke)
                    smoke_latch(&latch_jump, &latch_slide, &latch_dz, &quit);
                char line[80];
                int n = snprintf(line, sizeof(line), "INPUT %d %d %d %d %d\n", latch_jump, latch_slide,
                                 latch_dz, quit ? 1 : 0, latch_restart);
                latch_jump = latch_slide = latch_dz = latch_restart = 0;
                if (write_all(soft_in, line, (size_t)n) != 0)
                    break;
                if (quit)
                    break;
                awaiting = 1;
            }
            if (awaiting) {
                size_t n = 0;
                int rs = poll_snap(soft_out, snapbuf, SNAP_CAP, &n);
                if (rs == 0) {
                    ParkourSnap next;
                    memset(&next, 0, sizeof(next));
                    if (!parkour_sample_parse(snapbuf, n, &next) || !next.accepted) {
                        fprintf(stderr, "parkour_view: Soft SNAP parse failed\n");
                        break;
                    }
                    from = to;
                    to = next;
                    alpha = 0.0f;
                    awaiting = 0;
                    if (to.score - from.score >= 5)
                        coin_burst = 18;
                    log_snap(&to);
                    if (g_smoke && g_smoke_step >= 6)
                        quit = 1;
                } else if (rs < 0) {
                    fprintf(stderr, "parkour_view: Soft SNAP read failed (%d)\n", rs);
                    break;
                }
            }
        }
        parkour_present(&view, &from, &to, awaiting || frozen ? 1.0 : alpha);
        paint(&view, paused, coin_burst);
        if (coin_burst > 0)
            coin_burst--;
    }

    char bye[32];
    int n = snprintf(bye, sizeof(bye), "INPUT 0 0 0 1 0\n");
    write_all(soft_in, bye, (size_t)n);
    if (g_lit)
        UnloadShader(g_shader);
    UnloadMesh(g_cube);
    UnloadMesh(g_sphere);
    CloseWindow();
    close(soft_in);
    close(soft_out);
    if (soft_pid > 0) {
        kill(soft_pid, SIGTERM);
        waitpid(soft_pid, NULL, 0);
    }
    free(snapbuf);
    free(g_carry);
    return 0;
}
