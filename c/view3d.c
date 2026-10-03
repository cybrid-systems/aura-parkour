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
                         "out vec2 fragTexCoord;\n"
                         "out vec4 fragColor;\n"
                         "void main() {\n"
                         "  fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));\n"
                         "  fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 1.0)));\n"
                         "  fragTexCoord = vertexTexCoord;\n"
                         "  fragColor = vertexColor;\n"
                         "  gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
                         "}\n";

static const char *FS = "#version 330\n"
                         "in vec3 fragPosition;\n"
                         "in vec3 fragNormal;\n"
                         "in vec2 fragTexCoord;\n"
                         "in vec4 fragColor;\n"
                         "uniform sampler2D texture0;\n"
                         "uniform vec4 colDiffuse;\n"
                         "uniform vec3 viewPos;\n"
                         "out vec4 finalColor;\n"
                         "void main() {\n"
                         "  vec3 n = normalize(fragNormal);\n"
                         "  vec3 sun = normalize(vec3(-0.65, 0.42, 0.18));\n"
                         "  float diff = max(dot(n, sun), 0.0);\n"
                         "  float rim = pow(1.0 - max(dot(n, normalize(viewPos - fragPosition)), 0.0), 2.0);\n"
                         "  vec3 texel = texture(texture0, fragTexCoord).rgb;\n"
                         "  vec3 base = colDiffuse.rgb * fragColor.rgb * texel;\n"
                         "  vec3 sky = vec3(0.55, 0.28, 0.22);\n"
                         "  vec3 warm = vec3(1.0, 0.62, 0.32);\n"
                         "  vec3 lit = base * (sky * 0.42 + warm * diff) + warm * rim * 0.08;\n"
                         "  float dist = length(viewPos - fragPosition);\n"
                         "  float fog = clamp((dist - 5.0) / 28.0, 0.0, 1.0);\n"
                         "  vec3 fogCol = vec3(0.62, 0.30, 0.22);\n"
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
static Texture2D g_white;
static Texture2D g_floor_tex;
static Texture2D g_wall_tex;
static Texture2D g_tex;

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


static Texture2D finish_tex(Image img) {
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
    return tex;
}

static Texture2D make_white(void) {
    Image img = GenImageColor(2, 2, WHITE);
    return finish_tex(img);
}

static Texture2D make_floor(void) {
    Image img = GenImageColor(256, 256, (Color){42, 34, 28, 255});
    for (int gy = 0; gy < 8; gy++) {
        for (int gx = 0; gx < 8; gx++) {
            int alt = (gx + gy) & 1;
            Color tile = alt ? (Color){156, 128, 94, 255} : (Color){102, 82, 60, 255};
            ImageDrawRectangle(&img, gx * 32 + 2, gy * 32 + 2, 28, 28, tile);
            ImageDrawRectangle(&img, gx * 32 + 5, gy * 32 + 6, 10, 4, (Color){188, 160, 120, 255});
            ImageDrawRectangle(&img, gx * 32 + 18, gy * 32 + 16, 6, 6,
                               alt ? (Color){120, 96, 70, 255} : (Color){70, 56, 42, 255});
        }
    }
    return finish_tex(img);
}

static Texture2D make_wall(void) {
    Image img = GenImageColor(256, 128, (Color){54, 36, 32, 255});
    for (int row = 0; row < 8; row++) {
        int off = (row & 1) ? 16 : 0;
        for (int bx = -1; bx < 9; bx++) {
            Color brick = ((row + bx) & 1) ? (Color){176, 96, 68, 255} : (Color){146, 72, 52, 255};
            ImageDrawRectangle(&img, bx * 32 + off + 1, row * 16 + 1, 30, 14, brick);
            ImageDrawRectangle(&img, bx * 32 + off + 4, row * 16 + 3, 9, 3, (Color){204, 132, 96, 255});
            ImageDrawRectangle(&img, bx * 32 + off + 18, row * 16 + 8, 5, 3, (Color){110, 58, 44, 255});
        }
    }
    return finish_tex(img);
}

/* Top faces bright, sides mid, bottoms dark. Multiplies with the sunset light. */
static void shade_cube_faces(Mesh *mesh) {
    if (mesh->normals == NULL || mesh->vertexCount <= 0)
        return;
    if (mesh->colors == NULL)
        mesh->colors = calloc((size_t)mesh->vertexCount * 4, 1);
    if (mesh->colors == NULL)
        return;
    for (int i = 0; i < mesh->vertexCount; i++) {
        float ny = mesh->normals[i * 3 + 1];
        unsigned char v = 188;
        if (ny > 0.45f)
            v = 255;
        else if (ny < -0.45f)
            v = 110;
        mesh->colors[i * 4 + 0] = v;
        mesh->colors[i * 4 + 1] = v;
        mesh->colors[i * 4 + 2] = v;
        mesh->colors[i * 4 + 3] = 255;
    }
}

static void draw_mesh(Vector3 center, Vector3 size, Color color) {
    if (g_tex.id > 0)
        g_mat.maps[MATERIAL_MAP_DIFFUSE].texture = g_tex;
    g_mat.maps[MATERIAL_MAP_DIFFUSE].color = color;
    Matrix xform = MatrixMultiply(MatrixTranslate(center.x, center.y, center.z),
                                  MatrixScale(size.x, size.y, size.z));
    if (g_lit)
        DrawMesh(g_cube, g_mat, xform);
    else
        DrawCube(center, size.x, size.y, size.z, color);
}

static void draw_sphere(Vector3 center, float radius, Color color) {
    if (g_tex.id > 0)
        g_mat.maps[MATERIAL_MAP_DIFFUSE].texture = g_tex;
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
    g_tex = g_floor_tex.id > 0 ? g_floor_tex : g_white;
    for (int x = x0; x < x1; x += 2) {
        for (int lane = -2; lane <= 2; lane += 2) {
            float cx = (float)x + 1.0f;
            float cz = (float)lane;
            if (gap_here(s, cx, cz))
                continue;
            draw_mesh((Vector3){cx, -0.08f, cz}, (Vector3){2.0f, 0.16f, 1.85f}, WHITE);
        }
    }
    g_tex = g_wall_tex.id > 0 ? g_wall_tex : g_white;
    for (int x = x0; x < x1; x += 2) {
        float wx = (float)x + 1.0f;
        draw_mesh((Vector3){wx, 2.1f, -4.55f}, (Vector3){2.0f, 4.2f, 0.35f}, WHITE);
        draw_mesh((Vector3){wx, 2.1f, 4.55f}, (Vector3){2.0f, 4.2f, 0.35f}, WHITE);
        draw_mesh((Vector3){wx, 4.15f, -4.55f}, (Vector3){2.0f, 0.16f, 0.42f},
                  (Color){210, 170, 130, 255});
        draw_mesh((Vector3){wx, 4.15f, 4.55f}, (Vector3){2.0f, 0.16f, 0.42f},
                  (Color){210, 170, 130, 255});
    }
    g_tex = g_white;
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
            float spin = (float)GetTime() * 5.5f + cx;
            float pulse = 1.0f + 0.12f * sinf((float)GetTime() * 8.0f + cx);
            Color gold = {255, 214, 40, 255};
            g_mat.maps[MATERIAL_MAP_DIFFUSE].color = gold;
            Matrix face = MatrixMultiply(
                MatrixTranslate(cx, cy, cz),
                MatrixMultiply(MatrixRotateY(spin), MatrixScale(0.72f * pulse, 0.1f, 0.72f * pulse)));
            Matrix edge = MatrixMultiply(
                MatrixTranslate(cx, cy, cz),
                MatrixMultiply(MatrixRotateY(spin + 1.5708f),
                               MatrixScale(0.72f * pulse, 0.1f, 0.72f * pulse)));
            if (g_lit) {
                DrawMesh(g_cube, g_mat, face);
                DrawMesh(g_cube, g_mat, edge);
            } else {
                DrawCylinder((Vector3){cx, cy - 0.08f, cz}, 0.42f * pulse, 0.42f * pulse, 0.16f, 14, gold);
            }
            draw_sphere((Vector3){cx, cy, cz}, 0.16f * pulse, (Color){255, 244, 180, 255});
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

static void draw_leg(Vector3 hip, float swing, float tuck, float ground, Color pants, Color shoe) {
    Vector3 knee = {hip.x + swing * 0.55f, hip.y - 0.42f + fabsf(swing) * 0.08f, hip.z};
    Vector3 foot = {hip.x - swing * 0.35f + tuck, ground, hip.z};
    if (foot.y > knee.y - 0.05f)
        foot.y = knee.y - 0.05f;
    limb(hip, knee, 0.15f, pants);
    limb(knee, foot, 0.12f, pants);
    draw_mesh(foot, (Vector3){0.22f, 0.08f, 0.12f}, shoe);
}

static void draw_runner(const ParkourSnap *s) {
    g_tex = g_white;
    float x = (float)s->x + 0.2f;
    float y = (float)s->y;
    float z = (float)s->z;
    float phase = (float)s->x * 3.8f;
    float swing = 0.0f;
    float hip = 0.92f;
    float chest = 1.38f;
    float head = 1.72f;
    float ground = y + 0.06f;
    float tuck = 0.0f;
    int sliding = s->state == 2;
    int air = s->state == 1 || s->y > 0.25;
    if (sliding) {
        hip = 0.32f;
        chest = 0.48f;
        head = 0.62f;
        tuck = 0.55f;
        swing = 0.05f;
    } else if (air) {
        hip = 0.78f;
        chest = 1.22f;
        head = 1.58f;
        ground = y + 0.42f;
        tuck = 0.18f;
        swing = 0.22f;
    } else {
        swing = sinf(phase) * 0.55f;
    }
    float shadow = 1.0f / (1.0f + (float)s->y * 0.45f);
    draw_mesh((Vector3){x, 0.025f, z}, (Vector3){1.15f * shadow, 0.03f, 0.62f * shadow},
              (Color){28, 14, 12, 255});
    Color shirt = s->state == 3 ? (Color){176, 42, 42, 255} : (Color){24, 132, 214, 255};
    Color skin = {236, 196, 160, 255};
    Color pants = {18, 32, 72, 255};
    Color shoe = {230, 90, 40, 255};
    Color hair = {40, 26, 18, 255};
    draw_mesh((Vector3){x, y + (hip + chest) * 0.5f, z},
              (Vector3){sliding ? 0.7f : 0.42f, chest - hip, sliding ? 0.28f : 0.30f}, shirt);
    draw_leg((Vector3){x, y + hip, z - 0.12f}, swing, tuck, ground, pants, shoe);
    draw_leg((Vector3){x, y + hip, z + 0.12f}, -swing, tuck, ground, pants, shoe);
    float arm = air ? -0.35f : -swing;
    limb((Vector3){x, y + chest - 0.08f, z - 0.16f},
         (Vector3){x + arm * 0.7f, y + hip + 0.05f, z - 0.46f}, 0.1f, skin);
    limb((Vector3){x, y + chest - 0.08f, z + 0.16f},
         (Vector3){x - arm * 0.7f, y + hip + 0.05f, z + 0.46f}, 0.1f, skin);
    draw_sphere((Vector3){x, y + head, z}, 0.2f, skin);
    draw_mesh((Vector3){x, y + head + 0.06f, z}, (Vector3){0.28f, 0.12f, 0.26f}, hair);
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

static void draw_dust(const ParkourSnap *s) {
    static float prev_y = 0.0f;
    static int land = 0;
    g_tex = g_white;
    if (s->alive == 0 || s->state == 3) {
        prev_y = (float)s->y;
        return;
    }
    if (prev_y > 0.35f && s->y < 0.08f)
        land = 22;
    prev_y = (float)s->y;
    float x = (float)s->x + 0.15f;
    float z = (float)s->z;
    float spd = (float)s->vx;
    if (spd < 1.0f)
        spd = 1.0f;
    Color mote = {196, 160, 114, 255};
    Color puff = {226, 190, 140, 255};
    if (s->state == 2) {
        int n = 14;
        for (int i = 0; i < n; i++) {
            float t = (float)i / (float)(n - 1);
            float drift = sinf((float)GetTime() * 9.0f + (float)i) * 0.22f;
            draw_sphere((Vector3){x - 0.2f - t * (2.2f + spd * 0.35f), 0.08f + t * 0.35f, z + drift},
                        0.07f + t * 0.12f, mote);
        }
    } else if (s->y < 0.2f) {
        float phase = (float)s->x * 3.8f;
        int n = 8 + (int)spd;
        if (n > 18)
            n = 18;
        for (int i = 0; i < n; i++) {
            float k = (float)i;
            float hop = fmaxf(0.0f, sinf(phase - k * 0.55f));
            float side = ((i & 1) ? 0.22f : -0.22f) + sinf(phase * 0.5f + k) * 0.08f;
            draw_sphere((Vector3){x - 0.3f - k * (0.18f + spd * 0.045f), 0.05f + hop * 0.32f,
                                  z + side},
                        0.045f + hop * 0.09f, mote);
        }
    }
    if (land > 0) {
        float spread = (22 - land) * 0.16f + 0.28f;
        int n = 14;
        for (int i = 0; i < n; i++) {
            float ang = (float)i / (float)n * 6.28318f;
            float lift = 0.08f + spread * 0.35f * ((i % 3 == 0) ? 1.4f : 0.7f);
            draw_sphere((Vector3){x + cosf(ang) * spread * 1.15f, lift, z + sinf(ang) * spread * 0.7f},
                        0.08f + (float)(i % 2) * 0.05f, puff);
        }
        land--;
    }
}

static void draw_burst(const ParkourSnap *s, int frames) {
    if (frames <= 0)
        return;
    float k = (float)frames / 28.0f;
    for (int i = 0; i < 22; i++) {
        float ang = (float)i / 22.0f * 6.28318f + (float)GetTime() * 4.0f;
        float spread = (1.0f - k) * 2.8f + 0.2f;
        Vector3 p = {(float)s->x + 0.3f + cosf(ang) * spread * 0.45f,
                     (float)s->y + 1.2f + fabsf(sinf(ang)) * spread,
                     (float)s->z + sinf(ang * 2.0f) * spread * 0.8f};
        Color c = (i % 3 == 0) ? (Color){255, 250, 200, 255} : (Color){255, 196, 48, 255};
        draw_sphere(p, 0.08f + k * 0.16f, c);
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
    DrawText("space/w jump   s slide   a/d/arrows/drag lane   p pause   r restart   q quit", 16,
             h - 28, 16, (Color){180, 190, 210, 220});
    if (miss) {
        DrawRectangle(0, 0, w, 28, (Color){255, 176, 40, 160});
        DrawRectangle(0, h - 28, w, 28, (Color){255, 176, 40, 160});
    }
    if (burst > 0) {
        unsigned char a = (unsigned char)(40 + burst * 8);
        DrawRectangle(0, 0, w, h, (Color){255, 200, 60, a});
        DrawRectangleLinesEx((Rectangle){8, 8, (float)w - 16, (float)h - 16}, 4.0f,
                             (Color){255, 230, 90, 220});
    }
    if (!s->alive || s->state == 3) {
        DrawRectangle(0, 0, w, h, (Color){40, 0, 0, 90});
        DrawText("DEAD", w / 2 - 70, h / 2 - 30, 60, (Color){255, 80, 80, 255});
        DrawText("r restart    q quit", w / 2 - 110, h / 2 + 40, 24, RAYWHITE);
    }
}

static Camera3D chase_cam(const ParkourSnap *s) {
    static float lag_z = 0.0f;
    float bob = 0.0f;
    if (s->state == 0)
        bob = sinf((float)s->x * 2.4f) * 0.06f;
    /* Higher and further back, looking slightly down the lane. */
    float eye_y = (float)s->y + (s->state == 2 ? 1.85f : 3.65f) + bob;
    float fov = 50.0f + (float)s->vx * 5.0f;
    if (fov > 98.0f)
        fov = 98.0f;
    int miss = near_miss(s);
    float shake = 0.0f;
    if (miss) {
        fov += 8.0f;
        shake = sinf((float)GetTime() * 48.0f) * 0.1f;
    }
    float z = (float)s->z;
    lag_z += (z - lag_z) * 0.16f;
    float roll = (lag_z - z) * 0.28f;
    if (roll > 0.22f)
        roll = 0.22f;
    if (roll < -0.22f)
        roll = -0.22f;
    Camera3D cam = {0};
    cam.position = (Vector3){(float)s->x - 7.8f, eye_y + shake, z * 0.82f + shake * 0.5f};
    cam.target = (Vector3){(float)s->x + 10.0f, (float)s->y + 0.85f + shake * 0.25f, z};
    cam.up = (Vector3){sinf(roll), cosf(roll), 0.0f};
    cam.fovy = fov;
    cam.projection = CAMERA_PERSPECTIVE;
    return cam;
}

/* Screen-space sky. A mesh skybox would be eaten by the distance fog.
   The band is the same rust as fogCol in the fragment shader. */
static void draw_sky(void) {
    int w = GetScreenWidth();
    int h = GetScreenHeight();
    if (w < 1 || h < 1)
        return;
    Color top = {42, 52, 108, 255};
    Color mid = {214, 98, 52, 255};
    Color horizon = {158, 76, 56, 255}; /* 0.62, 0.30, 0.22 */
    Color ground = {48, 22, 18, 255};
    int band = h * 46 / 100;
    int blend = h / 8;
    if (blend < 8)
        blend = 8;
    DrawRectangleGradientV(0, 0, w, band, top, mid);
    DrawRectangleGradientV(0, band - blend, w, blend * 2, mid, horizon);
    DrawRectangleGradientV(0, band, w, h - band, horizon, ground);
    float sun_r = (float)h * 0.075f;
    Vector2 sun = {(float)w * 0.5f, (float)band - (float)h * 0.11f};
    DrawCircleV(sun, sun_r * 1.35f, (Color){255, 170, 80, 90});
    DrawCircleV(sun, sun_r, (Color){255, 196, 110, 230});
    DrawCircleV(sun, sun_r * 0.45f, (Color){255, 236, 200, 255});
}

static void draw_world(const ParkourSnap *s, int burst, Camera3D cam) {
    BeginMode3D(cam);
    if (g_lit)
        BeginShaderMode(g_shader);
    draw_corridor(s);
    draw_dust(s);
    draw_runner(s);
    draw_burst(s, burst);
    if (g_lit)
        EndShaderMode();
    EndMode3D();
}

static RenderTexture2D g_frame;
static RenderTexture2D g_prev;
static int g_blur_ready = 0;
static int g_have_prev = 0;

static void blit_rt(RenderTexture2D rt, Rectangle dest, Color tint) {
    Rectangle src = {0.0f, 0.0f, (float)rt.texture.width, -(float)rt.texture.height};
    DrawTexturePro(rt.texture, src, dest, (Vector2){0.0f, 0.0f}, 0.0f, tint);
}

static void paint(const ParkourSnap *s, int paused, int burst) {
    Camera3D cam = chase_cam(s);
    if (g_lit && g_view_loc >= 0) {
        float view[3] = {cam.position.x, cam.position.y, cam.position.z};
        SetShaderValue(g_shader, g_view_loc, view, SHADER_UNIFORM_VEC3);
    }
    float w = (float)GetScreenWidth();
    float h = (float)GetScreenHeight();
    if (g_blur_ready && w > 1.0f && h > 1.0f) {
        BeginTextureMode(g_frame);
        draw_sky();
        draw_world(s, burst, cam);
        EndTextureMode();
        BeginDrawing();
        if (g_have_prev) {
            /* Previous frame, nudged out, then the new frame mostly covers it. */
            blit_rt(g_prev, (Rectangle){-12.0f, -12.0f, w + 24.0f, h + 24.0f}, WHITE);
            blit_rt(g_frame, (Rectangle){0.0f, 0.0f, w, h}, (Color){255, 255, 255, 196});
        } else {
            blit_rt(g_frame, (Rectangle){0.0f, 0.0f, w, h}, WHITE);
        }
        draw_hud(s, paused, near_miss(s), burst);
        DrawFPS(GetScreenWidth() - 90, 14);
        EndDrawing();
        BeginTextureMode(g_prev);
        blit_rt(g_frame, (Rectangle){0.0f, 0.0f, (float)g_prev.texture.width, (float)g_prev.texture.height},
                WHITE);
        EndTextureMode();
        g_have_prev = 1;
        return;
    }
    BeginDrawing();
    draw_sky();
    draw_world(s, burst, cam);
    draw_hud(s, paused, near_miss(s), burst);
    DrawFPS(GetScreenWidth() - 90, 14);
    EndDrawing();
}

static int g_drag_on = 0;
static float g_drag_x = 0.0f;
static int g_drag_fired = 0;

static void latch_keys(int *jump, int *slide, int *dz, int *quit, int *restart, int *pause) {
    if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_W))
        *jump = 1;
    if (IsKeyPressed(KEY_S))
        *slide = 1;
    if (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT))
        *dz = -1;
    if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT))
        *dz = 1;
    /* One lane per press. A long drag or a horizontal scroll counts as a swipe. */
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        g_drag_on = 1;
        g_drag_x = 0.0f;
        g_drag_fired = 0;
    }
    if (g_drag_on && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        g_drag_x += GetMouseDelta().x;
        if (!g_drag_fired && g_drag_x >= 56.0f) {
            *dz = 1;
            g_drag_fired = 1;
        } else if (!g_drag_fired && g_drag_x <= -56.0f) {
            *dz = -1;
            g_drag_fired = 1;
        }
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        g_drag_on = 0;
    {
        Vector2 wheel = GetMouseWheelMoveV();
        if (wheel.x >= 0.45f)
            *dz = 1;
        else if (wheel.x <= -0.45f)
            *dz = -1;
    }
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
        g_shader.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(g_shader, "texture0");
    }
    g_white = make_white();
    g_floor_tex = make_floor();
    g_wall_tex = make_wall();
    g_tex = g_white;
    g_cube = GenMeshCube(1.0f, 1.0f, 1.0f);
    shade_cube_faces(&g_cube);
    g_sphere = GenMeshSphere(1.0f, 16, 12);
    g_mat = LoadMaterialDefault();
    if (g_lit)
        g_mat.shader = g_shader;
    g_frame = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
    g_prev = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
    g_blur_ready = g_frame.id != 0 && g_prev.id != 0;

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
                        coin_burst = 28;
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
    UnloadTexture(g_floor_tex);
    UnloadTexture(g_wall_tex);
    UnloadTexture(g_white);
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
