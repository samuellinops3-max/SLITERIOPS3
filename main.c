// Contra Novo - identico ao original - PS3 Homebrew PSL1GHT nativo
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <malloc.h>
#include <ppu-types.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <rsx/rsx.h>
#include <io/pad.h>

#define CB_SIZE   (0x100000)
#define HOST_SIZE (32 * 1024 * 1024)
#define WORLD  4800.0f
#define MAXEN  24
#define MAXBUL 64
#define MAXEBUL 32
#define MAXPLAT 14
#define PI     3.14159265f
#define GRAV   0.85f
#define GROUND_Y 620

typedef struct { float x, y, w, h; } Plat;
typedef struct { float x, y, vx, vy; int alive, hp, dir, type, shootTimer, anim; } Enemy;
typedef struct { float x, y, vx, vy; int alive, fromPlayer; } Bullet;

static const u32 palette[8] = {
    0x00FF4D4D, 0x0055D6FF, 0x0066FF66, 0x00FFD23F,
    0x00C77DFF, 0x00FF8FAB, 0x0000E5C0, 0x00FF9F43
};
static u32 use_palette(void){ return palette[0] + palette[1]; }

/* ---------- video ---------- */
static gcmContextData *ctx;
static u32 W, H;
static u32 *fb[2];
static u32 fboff[2];
static u32 *back;
static int cur = 0, first_flip = 1;
static volatile int running = 1;

static void sysutil_cb(u64 status, u64 param, void *usrdata)
{
    (void)param; (void)usrdata;
    if (status == SYSUTIL_EXIT_GAME) running = 0;
}

static void init_screen(void)
{
    void *host = memalign(1024 * 1024, HOST_SIZE);
    rsxInit(&ctx, CB_SIZE, HOST_SIZE, host);
    videoState st;
    videoGetState(VIDEO_PRIMARY, 0, &st);
    u32 resid = st.displayMode.resolution;
    if (videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720, VIDEO_ASPECT_AUTO, 0))
        resid = VIDEO_RESOLUTION_720;
    videoResolution res;
    videoGetResolution(resid, &res);
    W = res.width;
    H = res.height;
    videoConfiguration vc;
    memset(&vc, 0, sizeof(vc));
    vc.resolution = resid;
    vc.format = VIDEO_BUFFER_FORMAT_XRGB;
    vc.aspect = VIDEO_ASPECT_AUTO;
    vc.pitch = W * 4;
    videoConfigure(VIDEO_PRIMARY, &vc, NULL, 0);
    videoGetState(VIDEO_PRIMARY, 0, &st);
    gcmSetFlipMode(GCM_FLIP_VSYNC);
    for (int i = 0; i < 2; i++) {
        fb[i] = (u32 *)rsxMemalign(64, W * H * 4);
        gcmAddressToOffset(fb[i], &fboff[i]);
        gcmSetDisplayBuffer(i, fboff[i], W * 4, W, H);
    }
    back = (u32 *)memalign(64, W * H * 4);
    memset(back, 0, W * H * 4);
    gcmResetFlipStatus();
}

static void wait_flip(void)
{
    while (gcmGetFlipStatus() != 0) usleep(200);
    gcmResetFlipStatus();
}

static void present(void)
{
    if (!first_flip) wait_flip();
    else gcmResetFlipStatus();
    memcpy(fb[cur], back, W * H * 4);
    gcmSetFlip(ctx, cur);
    rsxFlushBuffer(ctx);
    gcmSetWaitFlip(ctx);
    cur ^= 1;
    first_flip = 0;
}

/* ---------- desenho ---------- */
static void fill_rect(int x, int y, int w, int h, u32 c)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w, y1 = y + h;
    if (x1 > (int)W) x1 = W;
    if (y1 > (int)H) y1 = H;
    for (int yy = y0; yy < y1; yy++) {
        u32 *p = back + yy * W;
        for (int xx = x0; xx < x1; xx++) p[xx] = c;
    }
}

static void circle(int cx, int cy, int r, u32 c)
{
    if (r < 1) r = 1;
    if (cx + r < 0 || cx - r >= (int)W || cy + r < 0 || cy - r >= (int)H) return;
    for (int dy = -r; dy <= r; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= (int)H) continue;
        int hw = (int)sqrtf((float)(r * r - dy * dy));
        int x0 = cx - hw, x1 = cx + hw;
        if (x0 < 0) x0 = 0;
        if (x1 >= (int)W) x1 = W - 1;
        u32 *p = back + y * W;
        for (int x = x0; x <= x1; x++) p[x] = c;
    }
}

typedef struct { char c; const char *p; } Glyph;
static const Glyph glyphs[] = {
    {'0', "###" "#.#" "#.#" "#.#" "###"}, {'1', ".#." "##." ".#." ".#." "###"},
    {'2', "###" "..#" "###" "#.." "###"}, {'3', "###" "..#" "###" "..#" "###"},
    {'4', "#.#" "#.#" "###" "..#" "..#"}, {'5', "###" "#.." "###" "..#" "###"},
    {'6', "###" "#.." "###" "#.#" "###"}, {'7', "###" "..#" "..#" "..#" "..#"},
    {'8', "###" "#.#" "###" "#.#" "###"}, {'9', "###" "#.#" "###" "..#" "###"},
    {'A', ".#." "#.#" "###" "#.#" "#.#"}, {'B', "##." "#.#" "##." "#.#" "##."},
    {'C', "###" "#.." "#.." "#.." "###"}, {'E', "###" "#.." "###" "#.." "###"},
    {'G', "###" "#.." "#.#" "#.#" "###"}, {'M', "#.#" "###" "###" "#.#" "#.#"},
    {'O', "###" "#.#" "#.#" "#.#" "###"}, {'P', "###" "#.#" "###" "#.." "#.."},
    {'R', "##." "#.#" "##." "#.#" "#.#"}, {'S', "###" "#.." "###" "..#" "###"},
    {'T', "###" ".#." ".#." ".#." ".#."}, {'U', "#.#" "#.#" "#.#" "#.#" "###"},
    {'V', "#.#" "#.#" "#.#" "#.#" ".#."}, {'X', "#.#" "#.#" ".#." "#.#" "#.#"},
    {'N', "#.#" "###" "###" "#.#" "#.#"}, {'L', "#.." "#.." "#.." "#.." "###"},
    {'I', "###" ".#." ".#." ".#." "###"}, {'D', "##." "#.#" "#.#" "#.#" "##."},
    {'F', "###" "#.." "###" "#.." "#.."}, {'H', "#.#" "#.#" "###" "#.#" "#.#"},
    {'J', "..#" "..#" "..#" "#.#" "###"}, {'K', "#.#" "#.#" "##." "#.#" "#.#"},
    {'W', "#.#" "#.#" "###" "###" "#.#"}, {'Y', "#.#" "#.#" ".#." ".#." ".#."},
    {'-', "..." "..." "###" "..." "..."}, {'!', ".#." ".#." ".#." "..." ".#."},
    {':', "..." ".#." "..." ".#." "..."}, {'/', "..#" "..#" ".#." "#.." "#.."},
};

static const char *find_glyph(char c)
{
    for (unsigned i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); i++)
        if (glyphs[i].c == c) return glyphs[i].p;
    return NULL;
}

static int text_width(const char *s, int sc) { return (int)strlen(s) * 4 * sc - sc; }

static void draw_text(const char *s, int x, int y, int sc, u32 c)
{
    for (; *s; s++) {
        const char *g = find_glyph(*s);
        if (g)
            for (int r = 0; r < 5; r++)
                for (int k = 0; k < 3; k++)
                    if (g[r * 3 + k] == '#') fill_rect(x + k * sc, y + r * sc, sc, sc, c);
        x += 4 * sc;
    }
}

/* ---------- jogo Contra ---------- */
static Plat plats[MAXPLAT];
static Enemy ens[MAXEN];
static Bullet buls[MAXBUL];
static Bullet ebul[MAXEBUL];
static float px = 120, py = 500, pvx = 0, pvy = 0;
static int pface = 1, ponGround = 1, pLives = 3, pScore = 0, pShootCd = 0, pAnim = 0, pDead = 0, pDeadT = 0, pInv = 0;
static int pAimUp = 0, pCrouch = 0;
static float camx = 0;
static int gameOver = 0, levelClear = 0, frame = 0, spawnTimer = 0;
static float pad_ax = 0, pad_ay = 0, pad_mag = 0;
static int pad_jump = 0, pad_jump_prev = 0, pad_shoot = 0, pad_x = 0, pad_x_prev = 0;

static float frand(void) { return (float)rand() / (float)RAND_MAX; }

static void init_platforms(void)
{
    plats[0].x = 0; plats[0].y = GROUND_Y; plats[0].w = 1400; plats[0].h = 100;
    plats[1].x = 1500; plats[1].y = GROUND_Y; plats[1].w = 900; plats[1].h = 100;
    plats[2].x = 2500; plats[2].y = GROUND_Y; plats[2].w = 1100; plats[2].h = 100;
    plats[3].x = 3700; plats[3].y = GROUND_Y; plats[3].w = 1100; plats[3].h = 100;
    plats[4].x = 400; plats[4].y = 480; plats[4].w = 220; plats[4].h = 20;
    plats[5].x = 800; plats[5].y = 380; plats[5].w = 220; plats[5].h = 20;
    plats[6].x = 1250; plats[6].y = 470; plats[6].w = 180; plats[6].h = 20;
    plats[7].x = 1750; plats[7].y = 380; plats[7].w = 220; plats[7].h = 20;
    plats[8].x = 2100; plats[8].y = 470; plats[8].w = 200; plats[8].h = 20;
    plats[9].x = 2750; plats[9].y = 380; plats[9].w = 220; plats[9].h = 20;
    plats[10].x = 3150; plats[10].y = 480; plats[10].w = 200; plats[10].h = 20;
    plats[11].x = 3600; plats[11].y = 380; plats[11].w = 220; plats[11].h = 20;
    plats[12].x = 4000; plats[12].y = 480; plats[12].w = 200; plats[12].h = 20;
    plats[13].x = 4350; plats[13].y = 380; plats[13].w = 220; plats[13].h = 20;
}

static void spawn_enemy_at(float x, float y, int type)
{
    for (int i = 0; i < MAXEN; i++) {
        if (!ens[i].alive) {
            ens[i].x = x; ens[i].y = y; ens[i].vx = 0; ens[i].vy = 0;
            ens[i].alive = 1; ens[i].type = type; ens[i].hp = (type == 1 ? 3 : 1);
            ens[i].dir = -1; ens[i].shootTimer = 60 + rand() % 80; ens[i].anim = 0;
            return;
        }
    }
}

static void init_game(void)
{
    memset(ens, 0, sizeof(ens));
    memset(buls, 0, sizeof(buls));
    memset(ebul, 0, sizeof(ebul));
    init_platforms();
    px = 120; py = 500; pvx = 0; pvy = 0; pface = 1; ponGround = 1;
    pLives = 3; pScore = 0; pShootCd = 0; pAnim = 0; pDead = 0; pDeadT = 0; pInv = 0;
    camx = 0; gameOver = 0; levelClear = 0; frame = 0; spawnTimer = 0;
    for (int i = 0; i < 6; i++) spawn_enemy_at(700 + i * 600 + frand() * 200, GROUND_Y - 40, 0);
    spawn_enemy_at(1600, 380 - 40, 1);
    spawn_enemy_at(2800, 380 - 40, 1);
    (void)use_palette();
}

static void player_shoot(void)
{
    if (pShootCd > 0) return;
    pShootCd = 9;
    for (int i = 0; i < MAXBUL; i++) {
        if (!buls[i].alive) {
            buls[i].alive = 1; buls[i].fromPlayer = 1;
            if (pAimUp) { buls[i].x = px + 14; buls[i].y = py - 10; buls[i].vx = 0; buls[i].vy = -11; }
            else if (pCrouch) { buls[i].x = px + 14 * pface; buls[i].y = py + 18; buls[i].vx = 11 * pface; buls[i].vy = 0; }
            else { buls[i].x = px + 14 * pface; buls[i].y = py + 6; buls[i].vx = 11 * pface; buls[i].vy = 0; }
            return;
        }
    }
}

static void enemy_shoot(Enemy *e)
{
    for (int i = 0; i < MAXEBUL; i++) {
        if (!ebul[i].alive) {
            ebul[i].alive = 1; ebul[i].fromPlayer = 0;
            ebul[i].x = e->x; ebul[i].y = e->y;
            float dx = px - e->x, dy = py - e->y;
            float d = sqrtf(dx*dx + dy*dy); if (d < 1) d = 1;
            ebul[i].vx = dx / d * 4.5f; ebul[i].vy = dy / d * 4.5f;
            return;
        }
    }
}

static int on_platform(float x, float y, float *landY)
{
    for (int i = 0; i < MAXPLAT; i++) {
        if (x >= plats[i].x - 10 && x <= plats[i].x + plats[i].w + 10) {
            if (y >= plats[i].y - 18 && y <= plats[i].y + 20) { *landY = plats[i].y; return 1; }
        }
    }
    return 0;
}

static void read_pad(void)
{
    padInfo info;
    padData d;
    pad_ax = pad_ay = pad_mag = 0;
    pad_jump = 0; pad_shoot = 0; pad_x = 0;
    ioPadGetInfo(&info);
    if (info.status[0]) {
        ioPadGetData(0, &d);
        float ax = ((int)d.ANA_L_H - 128) / 128.0f, ay = ((int)d.ANA_L_V - 128) / 128.0f;
        if (d.BTN_LEFT) ax = -1;
        if (d.BTN_RIGHT) ax = 1;
        if (d.BTN_UP) ay = -1;
        if (d.BTN_DOWN) ay = 1;
        if (fabsf(ax) < 0.18f) ax = 0;
        if (fabsf(ay) < 0.18f) ay = 0;
        pad_ax = ax; pad_ay = ay;
        pad_mag = sqrtf(ax * ax + ay * ay);
        pad_jump = d.BTN_CROSS;
        pad_shoot = d.BTN_SQUARE || d.BTN_R1 || d.BTN_R2;
        pad_x = d.BTN_CROSS;
    }
}

static void update_game(void)
{
    frame++;
    if (gameOver || levelClear) {
        if (pad_x && !pad_x_prev) init_game();
        pad_x_prev = pad_x;
        return;
    }
    if (pShootCd > 0) pShootCd--;
    if (pInv > 0) pInv--;
    if (!pDead) {
        pAimUp = (pad_ay < -0.5f);
        pCrouch = (pad_ay > 0.6f && ponGround);
        float speed = 4.2f;
        pvx = pad_ax * speed;
        if (pad_ax < -0.1f) pface = -1;
        if (pad_ax > 0.1f) pface = 1;
        if (fabsf(pvx) > 0.1f) pAnim++;
        px += pvx;
        if (px < 20) px = 20;
        if (px > WORLD - 40) px = WORLD - 40;
        if (pad_jump && !pad_jump_prev && ponGround) { pvy = -14.5f; ponGround = 0; }
        pvy += GRAV;
        if (pvy > 16) pvy = 16;
        float oldY = py;
        py += pvy;
        ponGround = 0;
        if (pvy >= 0) {
            float ly = 0;
            if (on_platform(px + 14, py + 40, &ly) && oldY + 40 <= ly + 18) { py = ly - 40; pvy = 0; ponGround = 1; }
        }
        if (py > 900) { pDead = 1; pDeadT = 0; }
        if (pad_shoot) player_shoot();
        float targetCam = px - W / 2.5f;
        if (targetCam < 0) targetCam = 0;
        if (targetCam > WORLD - W) targetCam = WORLD - W;
        if (targetCam < 0) targetCam = 0;
        camx += (targetCam - camx) * 0.12f;
        if (px > WORLD - 120) { levelClear = 1; pScore += 1000; }
    } else {
        pDeadT++;
        if (pDeadT > 90) {
            pLives--;
            if (pLives <= 0) gameOver = 1;
            else { px = camx + 100; if (px < 20) px = 120; py = 400; pvx = 0; pvy = 0; pDead = 0; pInv = 90; }
        }
    }
    pad_jump_prev = pad_jump;
    pad_x_prev = pad_x;
    spawnTimer++;
    if (spawnTimer > 70) {
        spawnTimer = 0;
        float sx = camx + W + 40 + frand() * 200;
        if (sx < WORLD - 100) spawn_enemy_at(sx, GROUND_Y - 40, rand() % 5 == 0 ? 1 : 0);
    }
    for (int i = 0; i < MAXEN; i++) {
        Enemy *e = &ens[i];
        if (!e->alive) continue;
        e->anim++;
        if (e->type == 0) {
            e->x += -1.3f;
            if (e->x < camx - 100) e->alive = 0;
        } else {
            e->shootTimer--;
            if (e->shootTimer <= 0) { e->shootTimer = 90 + rand() % 60; enemy_shoot(e); }
        }
        if (!pDead && pInv <= 0) {
            if (fabsf(e->x - (px + 14)) < 26 && fabsf(e->y - (py + 18)) < 34) { pDead = 1; pDeadT = 0; }
        }
    }
    for (int i = 0; i < MAXBUL; i++) {
        if (!buls[i].alive) continue;
        buls[i].x += buls[i].vx; buls[i].y += buls[i].vy;
        if (buls[i].x < camx - 50 || buls[i].x > camx + W + 50 || buls[i].y < -20 || buls[i].y > 720) { buls[i].alive = 0; continue; }
        for (int j = 0; j < MAXEN; j++) {
            Enemy *e = &ens[j];
            if (!e->alive) continue;
            if (fabsf(buls[i].x - e->x) < 24 && fabsf(buls[i].y - e->y) < 30) {
                buls[i].alive = 0; e->hp--;
                if (e->hp <= 0) { e->alive = 0; pScore += (e->type == 1 ? 300 : 100); }
                break;
            }
        }
    }
    for (int i = 0; i < MAXEBUL; i++) {
        if (!ebul[i].alive) continue;
        ebul[i].x += ebul[i].vx; ebul[i].y += ebul[i].vy;
        if (ebul[i].x < camx - 60 || ebul[i].x > camx + W + 60 || ebul[i].y < -20 || ebul[i].y > 760) { ebul[i].alive = 0; continue; }
        if (!pDead && pInv <= 0) {
            if (fabsf(ebul[i].x - (px + 14)) < 20 && fabsf(ebul[i].y - (py + 18)) < 28) { ebul[i].alive = 0; pDead = 1; pDeadT = 0; }
        }
    }
}

static void draw_player(int sx, int sy, float sc)
{
    int s = (int)(sc * 2);
    if (s < 2) s = 2;
    if (pDead) { fill_rect(sx, sy + 10, 28 * s / 2, 8 * s / 2, 0x00FF4D4D); return; }
    if (pInv > 0 && (frame / 6) % 2 == 0) return;
    int bodyH = pCrouch ? 22 : 38;
    int bodyY = sy + (pCrouch ? 18 : 0);
    int legSwing = (fabsf(pvx) > 0.1f && ponGround) ? ((pAnim / 5) % 2 ? 6 : -6) : 0;
    fill_rect(sx + 4, bodyY + bodyH, 6, 12, 0x001A3A8F);
    fill_rect(sx + 18, bodyY + bodyH, 6, 12, 0x001A3A8F);
    if (legSwing != 0) fill_rect(sx + 4 + legSwing, bodyY + bodyH + 10, 6, 4, 0x00000000);
    fill_rect(sx, bodyY, 28, bodyH, 0x00E82E2E);
    fill_rect(sx + 2, bodyY + 4, 24, 8, 0x00FF6B6B);
    fill_rect(sx + 6, bodyY - 14, 16, 14, 0x00FFD9B0);
    fill_rect(sx + 6, bodyY - 14, 16, 4, 0x00FF0000);
    fill_rect(sx + 4, bodyY - 12, 22, 4, 0x00FF0000);
    if (pAimUp) {
        fill_rect(sx + 10, bodyY - 10, 6, 18, 0x00FFD9B0);
        fill_rect(sx + 10, bodyY - 22, 6, 16, 0x00333333);
    } else {
        int ax = pface == 1 ? sx + 24 : sx - 8;
        fill_rect(ax, bodyY + 8, 14, 5, 0x00FFD9B0);
        fill_rect(ax + (pface == 1 ? 8 : -6), bodyY + 6, 16, 5, 0x00333333);
    }
}

static void render(void)
{
    float sc = (float)W / 1280.0f;
    u32 n = W * H;
    for (u32 i = 0; i < n; i++) back[i] = 0x000E1420;
    for (int y = 0; y < (int)H; y++) {
        int r = 12 + y / 30, g = 28 + y / 18, b = 70 + y / 10;
        if (r > 60) r = 60; if (g > 120) g = 120; if (b > 160) b = 160;
        u32 col = ((u32)r << 16) | ((u32)g << 8) | b;
        for (int x = 0; x < (int)W; x++) back[y * W + x] = col;
    }
    circle((int)(W - 180 - camx * 0.02f), 90, 42, 0x00FFD23F);
    circle((int)(W - 180 - camx * 0.02f), 90, 34, 0x00FFEA7A);
    for (int i = 0; i < 12; i++) {
        int mx = (int)(i * 420 - fmodf(camx * 0.3f, 420));
        int mh = 90 + (i * 37) % 110;
        for (int y = 0; y < mh; y++) {
            int wdt = mh - y;
            fill_rect(mx + y / 2, GROUND_Y - y, wdt, 2, 0x002A4A2A);
        }
    }
    for (int i = 0; i < MAXPLAT; i++) {
        int sx = (int)(plats[i].x - camx);
        int sy = plats[i].y;
        int wdt = (int)plats[i].w;
        if (sx + wdt < 0 || sx > (int)W) continue;
        if (plats[i].h > 50) {
            fill_rect(sx, sy, wdt, plats[i].h, 0x005A3A1A);
            fill_rect(sx, sy, wdt, 14, 0x003E8E2F);
            for (int x = sx; x < sx + wdt; x += 46) fill_rect(x + 8, sy + 22, 22, 6, 0x007A5A2A);
        } else {
            fill_rect(sx, sy, wdt, 14, 0x00B8860B);
            fill_rect(sx, sy + 14, wdt, 6, 0x005A3A1A);
        }
    }
    if (WORLD - camx < W + 200) {
        int bx = (int)(WORLD - 180 - camx);
        fill_rect(bx, 380, 180, 240, 0x00666A70);
        fill_rect(bx + 20, 400, 60, 60, 0x00FF4D4D);
        fill_rect(bx + 100, 400, 60, 60, 0x00FF4D4D);
        draw_text("BASE", bx + 50, 460, 3, 0x00FFFFFF);
    }
    for (int i = 0; i < MAXEN; i++) {
        Enemy *e = &ens[i];
        if (!e->alive) continue;
        int sx = (int)(e->x - camx), sy = (int)e->y;
        if (sx < -40 || sx > (int)W + 40) continue;
        if (e->type == 0) {
            int run = (e->anim / 6) % 2 ? 4 : -4;
            fill_rect(sx - 10, sy + 18, 20, 18, 0x003A8F3A);
            fill_rect(sx - 12 + run, sy + 36, 6, 10, 0x002A2A2A);
            fill_rect(sx + 6 - run, sy + 36, 6, 10, 0x002A2A2A);
            fill_rect(sx - 8, sy - 12, 16, 14, 0x00FFD9B0);
            fill_rect(sx - 8, sy - 12, 16, 4, 0x002A2A2A);
            fill_rect(sx - 16, sy + 6, 14, 5, 0x00333333);
        } else {
            fill_rect(sx - 14, sy - 10, 28, 50, 0x007A2A2A);
            fill_rect(sx - 10, sy - 24, 20, 16, 0x00FFD9B0);
            circle(sx, sy - 16, 8, 0x002A2A2A);
            fill_rect(sx - 20, sy + 10, 18, 6, 0x00333333);
        }
    }
    for (int i = 0; i < MAXBUL; i++) if (buls[i].alive) {
        int sx = (int)(buls[i].x - camx), sy = (int)buls[i].y;
        fill_rect(sx, sy, 10, 4, 0x00FFFF00);
        fill_rect(sx, sy - 1, 10, 2, 0x00FFFFFF);
    }
    for (int i = 0; i < MAXEBUL; i++) if (ebul[i].alive) {
        int sx = (int)(ebul[i].x - camx), sy = (int)ebul[i].y;
        circle(sx, sy, 5, 0x00FF4D4D);
    }
    draw_player((int)(px - camx), (int)py, sc);
    int ui = (int)(4 * sc + 0.5f);
    if (ui < 3) ui = 3;
    draw_text("SCORE", 24, 18, ui, 0x00FFFFFF);
    char buf[32];
    sprintf(buf, "%d", pScore);
    draw_text(buf, 24, 18 + 7 * ui, ui + 2, 0x00FFD23F);
    sprintf(buf, "LIVES %d", pLives);
    draw_text(buf, 24, 18 + 15 * ui, ui, 0x00FF8FAB);
    draw_text("CONTRA", (int)W / 2 - 60, 18, ui, 0x00FFD23F);
    draw_text("X PULA  QUADRADO ATIRA", 24, (int)H - 8 * ui, ui, 0x00809BBF);
    if (gameOver) {
        int bw = text_width("GAME OVER", ui * 2) + 80, bh = 16 * ui + 40;
        fill_rect((int)W / 2 - bw / 2, (int)H / 2 - bh / 2, bw, bh, 0x00000000);
        draw_text("GAME OVER", (int)W / 2 - text_width("GAME OVER", ui * 2) / 2, (int)H / 2 - bh / 2 + 24, ui * 2, 0x00FF4D4D);
        draw_text("PRESS X", (int)W / 2 - text_width("PRESS X", ui) / 2, (int)H / 2 + ui * 3, ui, 0x00FFFFFF);
    }
    if (levelClear) {
        int bw = text_width("MISSION CLEAR", ui * 2) + 80, bh = 16 * ui + 40;
        fill_rect((int)W / 2 - bw / 2, (int)H / 2 - bh / 2, bw, bh, 0x00000000);
        draw_text("MISSION CLEAR", (int)W / 2 - text_width("MISSION CLEAR", ui * 2) / 2, (int)H / 2 - bh / 2 + 24, ui * 2, 0x0066FF66);
        draw_text("PRESS X", (int)W / 2 - text_width("PRESS X", ui) / 2, (int)H / 2 + ui * 3, ui, 0x00FFFFFF);
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    init_screen();
    ioPadInit(7);
    sysUtilRegisterCallback(0, sysutil_cb, NULL);
    srand(20261109);
    init_game();
    while (running) {
        sysUtilCheckCallback();
                read_pad();
        update_game();
        render();
        present();
    }
    return 0;
}