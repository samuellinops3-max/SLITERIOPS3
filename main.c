/* main.c - Cobra Arena (base jogável) - PSL1GHT + Tiny3D
 * VERIFICAR: nomes dos campos do padData e assinaturas do tiny3d.h */

#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <sysutil/sysutil.h>
#include <io/pad.h>
#include <tiny3d.h>

#define SCR_W     1280.0f
#define SCR_H     720.0f
#define WORLD     2600.0f
#define NSNAKES   14
#define MAX_BODY  260
#define MAX_PEL   600
#define SEG       8.0f
#define DEADZONE  0.38f
#define BG_COLOR  0x161C22FFu
#define PI_F      3.14159265f
#define GRID      150.0f

typedef struct {
    float    x, y;
    uint32_t rgb;
    int      on;
    float    ph;
} Pellet;

typedef struct {
    float    hx, hy, ang;
    float    bx[MAX_BODY], by[MAX_BODY];
    int      len, alive, is_player, score, turbo;
    float    tacc, wander_t, wander_ang, respawn_t;
    uint32_t col;
} Snake;

static Snake  snakes[NSNAKES];
static Pellet pel[MAX_PEL];
static volatile int g_exit = 0;
static float camx = 0.0f, camy = 0.0f, zoom = 1.0f;
static uint32_t rng_state = 2463534242u;

static const uint32_t palette[NSNAKES] = {
    0x58FFB0, 0x3BD6FF, 0xA6FF3B, 0xFFD23B, 0xB23BFF, 0x3BFFB2, 0xFF8A3B,
    0x3B7BFF, 0xFF3BE0, 0x7CFF3B, 0x3BFFFF, 0xFFFF3B, 0xFF6B6B, 0x6BA8FF
};

/* ---------- utilitários ---------- */

static float frand(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) / 16777216.0f;
}

static float frange(float a, float b)
{
    return a + (b - a) * frand();
}

static float angdiff(float from, float to)
{
    float d = to - from;
    while (d >  PI_F) d -= 2.0f * PI_F;
    while (d < -PI_F) d += 2.0f * PI_F;
    return d;
}

static uint32_t lighten(uint32_t c)
{
    uint32_t r = (c >> 16) & 255u, g = (c >> 8) & 255u, b = c & 255u;
    r += (255u - r) / 2u;
    g += (255u - g) / 2u;
    b += (255u - b) / 2u;
    return (r << 16) | (g << 8) | b;
}

static float sx(float x) { return (x - camx) * zoom + SCR_W * 0.5f; }
static float sy(float y) { return (y - camy) * zoom + SCR_H * 0.5f; }

/* ---------- desenho ---------- */

/* Disco com degradê radial: centro com alpha 'a', borda com alpha 0.
 * VERIFICAR: sem tiny3d_SetBlend/alpha blending ativado, a borda fica dura. */
static void glow(float cx, float cy, float r, uint32_t rgb, uint32_t a)
{
    const int N = 10;
    uint32_t c0 = (rgb << 8) | (a & 255u);
    uint32_t c1 = (rgb << 8);
    tiny3d_SetPolygon(TINY3D_TRIANGLES);
    for (int i = 0; i < N; i++) {
        float a0 = (float)i       * 2.0f * PI_F / N;
        float a1 = (float)(i + 1) * 2.0f * PI_F / N;
        tiny3d_VertexPos(cx, cy, 0.0f);
        tiny3d_VertexColor(c0);
        tiny3d_VertexPos(cx + cosf(a0) * r, cy + sinf(a0) * r, 0.0f);
        tiny3d_VertexColor(c1);
        tiny3d_VertexPos(cx + cosf(a1) * r, cy + sinf(a1) * r, 0.0f);
        tiny3d_VertexColor(c1);
    }
    tiny3d_End();
}

static void draw_grid(void)
{
    float hw = SCR_W * 0.5f / zoom + GRID;
    float hh = SCR_H * 0.5f / zoom + GRID;
    float rowh = GRID * 0.866f;
    int j0 = (int)floorf((camy - hh) / rowh);
    int j1 = (int)floorf((camy + hh) / rowh);

    for (int j = j0; j <= j1; j++) {
        float off = (j & 1) ? GRID * 0.5f : 0.0f;
        float wy = (float)j * rowh;
        int i0 = (int)floorf((camx - hw - off) / GRID);
        int i1 = (int)floorf((camx + hw - off) / GRID);
        for (int i = i0; i <= i1; i++) {
            float wx = (float)i * GRID + off;
            if (wx * wx + wy * wy > WORLD * WORLD * 1.05f) continue;
            glow(sx(wx), sy(wy), 2.2f * zoom + 1.0f, 0x2A3A4A, 90);
        }
    }
}

static void draw_arena(void)
{
    for (int i = 0; i < 72; i++) {
        float a = (float)i * 2.0f * PI_F / 72.0f;
        glow(sx(cosf(a) * WORLD), sy(sinf(a) * WORLD), 9.0f * zoom, 0xFF3355, 230);
    }
}

static void draw_pellets(void)
{
    for (int p = 0; p < MAX_PEL; p++) {
        if (!pel[p].on) continue;
        float x = sx(pel[p].x), y = sy(pel[p].y);
        if (x < -60 || x > SCR_W + 60 || y < -60 || y > SCR_H + 60) continue;
        pel[p].ph += 0.12f;
        float r = 8.0f * zoom * (1.0f + 0.22f * sinf(pel[p].ph));
        glow(x, y, r * 2.6f, pel[p].rgb, 60);
        glow(x, y, r,        pel[p].rgb, 255);
        glow(x, y, r * 0.4f, 0xFFFFFF,   255);
    }
}

static void draw_snake(Snake *s, int full_detail)
{
    float R = 10.0f + s->len * 0.02f;
    if (R > 20.0f) R = 20.0f;
    float r = R * zoom;
    int step = full_detail ? 1 : 2;

    /* corpo: do rabo para a cabeça, com sombra e highlight */
    for (int i = s->len - 1; i >= 0; i -= step) {
        float x = sx(s->bx[i]), y = sy(s->by[i]);
        if (full_detail)
            glow(x + 3.0f * zoom, y + 5.0f * zoom, r * 1.05f, 0x000000, 90);
        glow(x, y, r, (uint32_t)s->col, 255);
        glow(x - r * 0.25f, y - r * 0.3f, r * 0.45f, lighten(s->col), 120);
    }

    /* cabeça */
    float hx = sx(s->hx), hy = sy(s->hy);
    float hr = r * 1.15f;
    glow(hx + 3.0f * zoom, hy + 5.0f * zoom, hr * 1.05f, 0x000000, 90);
    glow(hx, hy, hr, (uint32_t)s->col, 255);
    glow(hx - hr * 0.2f, hy - hr * 0.3f, hr * 0.4f, lighten(s->col), 140);

    /* olhos */
    float fx = cosf(s->ang), fy = sinf(s->ang);
    float px = -fy, py = fx;
    float ex = hx + fx * hr * 0.4f, ey = hy + fy * hr * 0.4f;
    glow(ex + px * hr * 0.45f, ey + py * hr * 0.45f, hr * 0.22f, 0xFFFFFF, 255);
    glow(ex - px * hr * 0.45f, ey - py * hr * 0.45f, hr * 0.22f, 0xFFFFFF, 255);
}

static void draw_minimap(void)
{
    float mx = SCR_W - 95.0f, my = 95.0f, mr = 70.0f;
    glow(mx, my, mr * 1.05f, 0x000000, 140);
    for (int k = NSNAKES - 1; k >= 0; k--) {
        Snake *s = &snakes[k];
        if (!s->alive) continue;
        float x = mx + s->hx / WORLD * mr;
        float y = my + s->hy / WORLD * mr;
        glow(x, y, k == 0 ? 5.0f : 3.0f, s->col, 255);
    }
}

/* ---------- entrada ---------- */

static void read_input(float *mx, float *my, int *moving, int *boost, int *cross)
{
    padInfo info;
    padData d;

    *mx = *my = 0.0f;
    *moving = *boost = *cross = 0;

    if (ioPadGetInfo(&info) != 0) return;
    if (!info.status[0])          return;
    if (ioPadGetData(0, &d) != 0) return;

    /* VERIFICAR nomes dos campos em io/pad.h */
    float lx = ((float)d.ANA_L_H - 128.0f) / 127.0f;
    float ly = -((float)d.ANA_L_V - 128.0f) / 127.0f;
    float m = sqrtf(lx * lx + ly * ly);
    if (m > DEADZONE) {
        *mx = lx / m;
        *my = ly / m;
        *moving = 1;
    }

    /* R2 digital aqui: a pressão >80 precisa de campo analógico (VERIFICAR) */
    *cross = d.BTN_CROSS ? 1 : 0;
    *boost = (d.BTN_CROSS || d.BTN_R2) ? 1 : 0;
}

/* ---------- mundo ---------- */

static void drop_pel(float x, float y, uint32_t rgb)
{
    for (int i = 0; i < MAX_PEL; i++) {
        if (!pel[i].on) {
            pel[i].on = 1;
            pel[i].x = x;
            pel[i].y = y;
            pel[i].rgb = rgb;
            return;
        }
    }
}

static void spawn_pel_random(int i)
{
    float a = frange(0.0f, 2.0f * PI_F);
    float r = WORLD * 0.95f * sqrtf(frand());
    pel[i].x = cosf(a) * r;
    pel[i].y = sinf(a) * r;
    pel[i].rgb = palette[rng_state % NSNAKES];
    pel[i].on = 1;
    pel[i].ph = frange(0.0f, 6.28f);
}

static void spawn(Snake *s, int player)
{
    float a = frange(0.0f, 2.0f * PI_F);
    float r = WORLD * 0.6f * sqrtf(frand());
    s->hx = cosf(a) * r;
    s->hy = sinf(a) * r;
    s->ang = frange(-PI_F, PI_F);
    s->len = 6;
    s->alive = 1;
    s->is_player = player;
    s->score = 0;
    s->turbo = 0;
    s->tacc = 0.0f;
    s->wander_t = 0.0f;
    s->wander_ang = s->ang;
    s->respawn_t = 0.0f;
    for (int i = 0; i < s->len; i++) {
        s->bx[i] = s->hx - cosf(s->ang) * SEG * (float)(i + 1);
        s->by[i] = s->hy - sinf(s->ang) * SEG * (float)(i + 1);
    }
}

static void grow(Snake *s)
{
    if (s->len >= MAX_BODY) return;
    int i = s->len;
    s->bx[i] = (i > 0) ? s->bx[i - 1] : s->hx;
    s->by[i] = (i > 0) ? s->by[i - 1] : s->hy;
    s->len++;
}

static void kill_snake(Snake *s)
{
    if (!s->alive) return;
    s->alive = 0;
    for (int i = 0; i < s->len; i += 2)
        drop_pel(s->bx[i], s->by[i], s->col);
    drop_pel(s->hx, s->hy, s->col);
    if (!s->is_player)
        s->respawn_t = 2.5f;
}

/* Alvo do bot: parede > corpo > comida > vagar */
static float bot_target(Snake *s)
{
    float r = sqrtf(s->hx * s->hx + s->hy * s->hy);
    if (r > WORLD * 0.85f)
        return atan2f(-s->hy, -s->hx);

    float fx = cosf(s->ang), fy = sinf(s->ang);
    for (int k = 0; k < NSNAKES; k++) {
        Snake *o = &snakes[k];
        if (!o->alive) continue;
        for (int i = 0; i < o->len; i += 2) {
            if (o == s && i < 12) continue;
            float dx = o->bx[i] - s->hx, dy = o->by[i] - s->hy;
            if (dx * dx + dy * dy < 80.0f * 80.0f && dx * fx + dy * fy > 0.0f) {
                float side = dx * fy - dy * fx;
                return s->ang + (side > 0.0f ? -1.4f : 1.4f);
            }
        }
    }

    float best = 380.0f * 380.0f, tx = 0.0f, ty = 0.0f;
    int found = 0;
    for (int p = 0; p < MAX_PEL; p++) {
        if (!pel[p].on) continue;
        float dx = pel[p].x - s->hx, dy = pel[p].y - s->hy;
        float d2 = dx * dx + dy * dy;
        if (d2 < best) { best = d2; tx = pel[p].x; ty = pel[p].y; found = 1; }
    }
    if (found) return atan2f(ty - s->hy, tx - s->hx);

    s->wander_t -= 1.0f / 60.0f;
    if (s->wander_t <= 0.0f) {
        s->wander_t = frange(1.0f, 3.0f);
        s->wander_ang = frange(-PI_F, PI_F);
    }
    return s->wander_ang;
}

static void step(Snake *s, float tgt, float dt, int moving, int turbo)
{
    if (moving) {
        float d = angdiff(s->ang, tgt);
        float mt = 3.4f * dt;
        if (d >  mt) d =  mt;
        if (d < -mt) d = -mt;
        s->ang += d;
        float sp = turbo ? 240.0f : 135.0f;
        s->hx += cosf(s->ang) * sp * dt;
        s->hy += sinf(s->ang) * sp * dt;
    }

    /* turbo: perde tamanho e solta rastro */
    if (turbo && moving && s->len > 8) {
        s->tacc += dt * 7.0f;
        if (s->tacc >= 1.0f) {
            s->tacc -= 1.0f;
            drop_pel(s->bx[s->len - 1], s->by[s->len - 1], s->col);
            s->len--;
        }
    }

    /* corpo segue a cabeça mantendo espaçamento fixo */
    float px = s->hx, py = s->hy;
    for (int i = 0; i < s->len; i++) {
        float dx = px - s->bx[i], dy = py - s->by[i];
        float dd = sqrtf(dx * dx + dy * dy);
        if (dd > SEG) {
            float k = (dd - SEG) / dd;
            s->bx[i] += dx * k;
            s->by[i] += dy * k;
        }
        px = s->bx[i];
        py = s->by[i];
    }
}

/* ---------- jogo ---------- */

static void init_game(void)
{
    for (int k = 0; k < NSNAKES; k++) {
        snakes[k].col = palette[k];
        spawn(&snakes[k], k == 0);
    }
    for (int p = 0; p < 450; p++)
        spawn_pel_random(p);
}

static void update(void)
{
    const float dt = 1.0f / 60.0f;
    float mx, my;
    int moving, boost, cross;
    read_input(&mx, &my, &moving, &boost, &cross);

    Snake *P = &snakes[0];

    /* jogador */
    if (P->alive) {
        float tgt = moving ? atan2f(my, mx) : P->ang;
        int turbo = boost && moving && P->len > 8;
        P->turbo = turbo;
        step(P, tgt, dt, moving, turbo);
    } else if (cross) {
        spawn(P, 1);
        camx = P->hx;
        camy = P->hy;
    }

    /* bots */
    for (int k = 1; k < NSNAKES; k++) {
        Snake *s = &snakes[k];
        if (!s->alive) {
            s->respawn_t -= dt;
            if (s->respawn_t <= 0.0f) spawn(s, 0);
            continue;
        }
        step(s, bot_target(s), dt, 1, 0);
    }

    /* colisões: parede, corpos */
    for (int a = 0; a < NSNAKES; a++) {
        Snake *sa = &snakes[a];
        if (!sa->alive) continue;

        int dead = (sa->hx * sa->hx + sa->hy * sa->hy > WORLD * WORLD);

        for (int b = 0; b < NSNAKES && !dead; b++) {
            Snake *sb = &snakes[b];
            if (!sb->alive) continue;
            int start = (a == b) ? 10 : 0;
            for (int i = start; i < sb->len; i++) {
                float dx = sb->bx[i] - sa->hx, dy = sb->by[i] - sa->hy;
                if (dx * dx + dy * dy < 7.0f * 7.0f) { dead = 1; break; }
            }
        }

        if (dead) kill_snake(sa);
    }

    /* comida */
    for (int k = 0; k < NSNAKES; k++) {
        Snake *s = &snakes[k];
        if (!s->alive) continue;
        for (int p = 0; p < MAX_PEL; p++) {
            if (!pel[p].on) continue;
            float dx = pel[p].x - s->hx, dy = pel[p].y - s->hy;
            if (dx * dx + dy * dy < 14.0f * 14.0f) {
                spawn_pel_random(p);
                grow(s);
                s->score++;
            }
        }
    }

    /* câmera e zoom */
    if (P->alive) {
        camx += (P->hx - camx) * 0.12f;
        camy += (P->hy - camy) * 0.12f;
    }
}

static void render(void)
{
    Snake *P = &snakes[0];

    tiny3d_Clear(BG_COLOR, TINY3D_CLEAR_ALL);
    tiny3d_Project2D();

    float tz = 1.0f / (0.8f + (float)P->len / 500.0f);
    if (tz < 0.42f) tz = 0.42f;
    if (tz > 1.0f)  tz = 1.0f;
    zoom += (tz - zoom) * 0.04f;

    draw_grid();
    draw_arena();
    draw_pellets();

    for (int k = NSNAKES - 1; k >= 1; k--)
        if (snakes[k].alive) draw_snake(&snakes[k], 0);
    if (P->alive) draw_snake(P, 1);

    draw_minimap();

    if (!P->alive)
        glow(SCR_W * 0.5f, SCR_H * 0.5f, 400.0f, 0x000000, 110);
}

/* ---------- sistema ---------- */

static void sysutil_cb(uint64_t status, uint64_t param, void *userdata)
{
    (void)param;
    (void)userdata;
    if (status == SYSUTIL_EXIT_GAME)
        g_exit = 1;
}

int main(void)
{
    sysRegisterCallback(EVENT_SLOT0, sysutil_cb, NULL);
    ioPadInit(7);
    tiny3d_Init(4 * 1024 * 1024);   /* VERIFICAR tamanho do buffer */

    init_game();

    while (!g_exit) {
        update();
        render();
        tiny3d_Flip();
    }

    ioPadEnd();
    return 0;
}