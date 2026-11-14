#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include <ppu-types.h>
#include <sysutil/sysutil.h>
#include <io/pad.h>
#include <tiny3d.h>

#define VIRTUAL_W   848.0f
#define VIRTUAL_H   512.0f
#define MAP_SIZE    4096.0f
#define NUM_SNAKES  14
#define NUM_FOODS   350
#define HIST_SIZE   1200
#define MAX_LEN     220
#define PI          3.14159265f

typedef struct { float x, y; } Vec2;

typedef struct {
    Vec2 pos;
    Vec2 hist[HIST_SIZE];
    int hist_head;
    float angle;
    float target_angle;
    int length;
    u32 color;
    u32 dark_color;
    int alive;
    int respawn;
    int is_bot;
    int ai_timer;
    int boost;
    int tick;
} Snake;

typedef struct {
    Vec2 pos;
    u32 color;
    float radius;
    float pulse;
    int active;
} Food;

static volatile int running = 1;
static void sys_cb(u64 status, u64 param, void *userdata) {
    (void)param; (void)userdata;
    if (status == SYSUTIL_EXIT_GAME) running = 0;
}

static Snake snakes[NUM_SNAKES];
static Food foods[NUM_FOODS];

static u32 *tex_circle = NULL, off_circle = 0;
static u32 *tex_glow   = NULL, off_glow   = 0;
static u32 *tex_bg     = NULL, off_bg     = 0;
static u32 *tex_white  = NULL, off_white  = 0;

static u32 make_rgba(u8 r, u8 g, u8 b, u8 a) {
    return ((u32)a << 24) | ((u32)r << 16) | ((u32)g << 8) | b;
}

static u32 darken_col(u32 c, float f) {
    u8 r = (u8)(((c >> 16) & 0xFF) * f);
    u8 g = (u8)(((c >> 8) & 0xFF) * f);
    u8 b = (u8)((c & 0xFF) * f);
    return make_rgba(r, g, b, 255);
}
static void build_textures(void) {
    tex_circle = (u32 *)tiny3d_AllocTexture(128 * 128 * 4);
    tex_glow   = (u32 *)tiny3d_AllocTexture(128 * 128 * 4);
    tex_bg     = (u32 *)tiny3d_AllocTexture(256 * 256 * 4);
    tex_white  = (u32 *)tiny3d_AllocTexture(4 * 4 * 4);

    for (int i = 0; i < 16; i++) tex_white[i] = 0xFFFFFFFF;

    for (int y = 0; y < 128; y++) {
        for (int x = 0; x < 128; x++) {
            float dx = (x - 63.5f) / 63.5f;
            float dy = (y - 63.5f) / 63.5f;
            float dist = sqrtf(dx * dx + dy * dy);

            u8 a_circ = 0;
            if (dist < 1.0f) {
                float edge = (1.0f - dist) / 0.08f;
                if (edge > 1.0f) edge = 1.0f;
                a_circ = (u8)(edge * 255.0f);
            }
            tex_circle[y * 128 + x] = make_rgba(255, 255, 255, a_circ);

            u8 a_glow = (dist < 1.0f) ? (u8)(expf(-dist * dist * 4.2f) * 255.0f) : 0;
            tex_glow[y * 128 + x] = make_rgba(255, 255, 255, a_glow);
        }
    }

    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            float q = (float)x * 0.8660254f;
            float r = (float)y - (float)x * 0.5f;
            float gx = fmodf(q, 64.0f); if (gx < 0.0f) gx += 64.0f;
            float gy = fmodf(r, 64.0f); if (gy < 0.0f) gy += 64.0f;
            float d = fminf(fminf(gx, 64.0f - gx), fminf(gy, 64.0f - gy));
            if (d < 1.3f) {
                u8 v = (u8)(0x26 + (1.0f - d / 1.3f) * 16.0f);
                tex_bg[y * 256 + x] = make_rgba(v, v + 6, v + 12, 255);
            } else {
                tex_bg[y * 256 + x] = 0xFF161C22;
            }
        }
    }

    off_circle = tiny3d_TextureOffset(tex_circle);
    off_glow   = tiny3d_TextureOffset(tex_glow);
    off_bg     = tiny3d_TextureOffset(tex_bg);
    off_white  = tiny3d_TextureOffset(tex_white);
}

static void draw_quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, u32 col, u32 off, int tw, int th) {
    tiny3d_SetTexture(0, off, tw, th, tw * 4, TINY3D_TEX_FORMAT_A8R8G8B8, 1);
    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(x, y, 0.0f);         tiny3d_VertexColor(col); tiny3d_VertexTexture(u0, v0);
    tiny3d_VertexPos(x + w, y, 0.0f);     tiny3d_VertexColor(col); tiny3d_VertexTexture(u1, v0);
    tiny3d_VertexPos(x + w, y + h, 0.0f); tiny3d_VertexColor(col); tiny3d_VertexTexture(u1, v1);
    tiny3d_VertexPos(x, y + h, 0.0f);     tiny3d_VertexColor(col); tiny3d_VertexTexture(u0, v1);
    tiny3d_End();
}

static void draw_circle(float cx, float cy, float diam, u32 col) {
    float r = diam * 0.5f;
    draw_quad(cx - r, cy - r, diam, diam, 0.0f, 0.0f, 1.0f, 1.0f, col, off_circle, 128, 128);
}

static void draw_glow(float cx, float cy, float diam, u32 col) {
    tiny3d_SetBlendMode(TINY3D_BLEND_FUNC_SRC_ALPHA, TINY3D_BLEND_FUNC_ONE);
    float r = diam * 0.5f;
    draw_quad(cx - r, cy - r, diam, diam, 0.0f, 0.0f, 1.0f, 1.0f, col, off_glow, 128, 128);
    tiny3d_SetBlendMode(TINY3D_BLEND_FUNC_SRC_ALPHA, TINY3D_BLEND_FUNC_ONE_MINUS_SRC_ALPHA);
}

static void spawn_food(int idx, float x, float y, float rad) {
    static const u32 pal[] = { 0xFFFF4D6D, 0xFFFFD166, 0xFF06D6A0, 0xFF118AB2, 0xFF9B5DE5, 0xFFF15BB5, 0xFF00F5D4, 0xFFFE7A00 };
    foods[idx].pos.x = x;
    foods[idx].pos.y = y;
    foods[idx].radius = rad;
    foods[idx].color = pal[rand() % 8];
    foods[idx].pulse = (float)(rand() % 100) * 0.1f;
    foods[idx].active = 1;
}

static void spawn_snake(int idx) {
    static const u32 spal[] = { 0xFF22EE77, 0xFFFF3366, 0xFF33CCFF, 0xFFFFCC33, 0xFFCC66FF, 0xFFFF6633, 0xFF00F5D4, 0xFF9BF00B };
    Snake *s = &snakes[idx];
    s->pos.x = 400.0f + (float)(rand() % (int)(MAP_SIZE - 800.0f));
    s->pos.y = 400.0f + (float)(rand() % (int)(MAP_SIZE - 800.0f));
    s->angle = ((float)(rand() % 628)) / 100.0f;
    s->target_angle = s->angle;
    s->length = (idx == 0) ? 20 : (16 + rand() % 25);
    s->color = (idx == 0) ? 0xFF22EE77 : spal[idx % 8];
    s->dark_color = darken_col(s->color, 0.72f);
    s->alive = 1;
    s->respawn = 0;
    s->is_bot = (idx != 0);
    s->ai_timer = 0;
    s->boost = 0;
    s->tick = 0;
    s->hist_head = 0;
    for (int i = 0; i < HIST_SIZE; i++) {
        s->hist[i].x = s->pos.x - cosf(s->angle) * (i * 2.0f);
        s->hist[i].y = s->pos.y - sinf(s->angle) * (i * 2.0f);
    }
}

static void kill_snake(int idx) {
    Snake *s = &snakes[idx];
    s->alive = 0;
    s->respawn = 180;
    for (int j = 0; j < s->length; j += 2) {
        int hi = (s->hist_head - (j * 4) + HIST_SIZE) % HIST_SIZE;
        for (int f = 0; f < NUM_FOODS; f++) {
            if (!foods[f].active) {
                spawn_food(f, s->hist[hi].x + (rand() % 16 - 8), s->hist[hi].y + (rand() % 16 - 8), 7.5f);
                foods[f].color = s->color;
                break;
            }
        }
    }
}
int main(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    padInfo padinfo;
    padData paddata;
    memset(&paddata, 0, sizeof(paddata));

    tiny3d_Init(1024 * 1024 * 2);
    ioPadInit(7);
    sysUtilRegisterCallback(0, sys_cb, NULL);

    build_textures();
    for (int i = 0; i < NUM_SNAKES; i++) spawn_snake(i);
    for (int i = 0; i < NUM_FOODS; i++) {
        spawn_food(i, 100.0f + (rand() % (int)(MAP_SIZE - 200.0f)), 100.0f + (rand() % (int)(MAP_SIZE - 200.0f)), 5.0f + (rand() % 4));
    }

    float cam_x = snakes[0].pos.x, cam_y = snakes[0].pos.y, cam_zoom = 1.0f;

    while (running) {
        sysUtilCheckCallback();
        ioPadGetInfo(&padinfo);
        if (padinfo.status[0]) ioPadGetData(0, &paddata);

        if (snakes[0].alive) {
            float lx = ((float)paddata.ANA_L_H - 128.0f) / 128.0f;
            float ly = ((float)paddata.ANA_L_V - 128.0f) / 128.0f;
            if (paddata.BTN_LEFT)  lx = -1.0f;
            if (paddata.BTN_RIGHT) lx =  1.0f;
            if (paddata.BTN_UP)    ly = -1.0f;
            if (paddata.BTN_DOWN)  ly =  1.0f;
            if (sqrtf(lx * lx + ly * ly) > 0.38f) snakes[0].target_angle = atan2f(ly, lx);
            snakes[0].boost = (paddata.BTN_CROSS || paddata.BTN_R2 || paddata.PRE_R2 > 80) ? 1 : 0;
        } else if (paddata.BTN_CROSS || paddata.BTN_START) {
            spawn_snake(0);
        }

        for (int i = 0; i < NUM_SNAKES; i++) {
            Snake *s = &snakes[i];
            if (!s->alive) {
                if (s->is_bot && --s->respawn <= 0) spawn_snake(i);
                continue;
            }
            s->tick++;
            if (s->is_bot) {
                if (++s->ai_timer > 45) {
                    s->target_angle += ((float)(rand() % 100) - 50.0f) / 40.0f;
                    s->ai_timer = 0;
                }
                if (s->pos.x < 250.0f || s->pos.x > MAP_SIZE - 250.0f || s->pos.y < 250.0f || s->pos.y > MAP_SIZE - 250.0f) {
                    s->target_angle = atan2f((MAP_SIZE * 0.5f) - s->pos.y, (MAP_SIZE * 0.5f) - s->pos.x);
                }
            }

            float diff = s->target_angle - s->angle;
            while (diff > PI)  diff -= 2.0f * PI;
            while (diff < -PI) diff += 2.0f * PI;
            s->angle += diff * 0.11f;

            float spd = 3.2f;
            if (s->boost && s->length > 12) {
                spd = 6.2f;
                if (s->tick % 14 == 0) {
                    s->length--;
                    for (int f = 0; f < NUM_FOODS; f++) {
                        if (!foods[f].active) { spawn_food(f, s->pos.x, s->pos.y, 4.5f); foods[f].color = s->color; break; }
                    }
                }
            }
            s->pos.x += cosf(s->angle) * spd;
            s->pos.y += sinf(s->angle) * spd;

            if (s->pos.x < 30.0f || s->pos.x > MAP_SIZE - 30.0f || s->pos.y < 30.0f || s->pos.y > MAP_SIZE - 30.0f) {
                kill_snake(i); continue;
            }

            s->hist_head = (s->hist_head + 1) % HIST_SIZE;
            s->hist[s->hist_head] = s->pos;

            float rad = 10.0f + (s->length * 0.08f);
            if (rad > 24.0f) rad = 24.0f;

            for (int f = 0; f < NUM_FOODS; f++) {
                if (!foods[f].active) continue;
                float dx = foods[f].pos.x - s->pos.x, dy = foods[f].pos.y - s->pos.y;
                float er = rad + foods[f].radius + 6.0f;
                if ((dx * dx + dy * dy) < (er * er)) {
                    if (s->length < MAX_LEN) s->length++;
                    spawn_food(f, 100.0f + (rand() % (int)(MAP_SIZE - 200.0f)), 100.0f + (rand() % (int)(MAP_SIZE - 200.0f)), 5.0f + (rand() % 4));
                }
            }

            for (int o = 0; o < NUM_SNAKES; o++) {
                if (o == i || !snakes[o].alive) continue;
                float orad = 10.0f + (snakes[o].length * 0.08f);
                if (orad > 24.0f) orad = 24.0f;
                float hd = (rad + orad) * 0.75f;
                for (int seg = 2; seg < snakes[o].length; seg++) {
                    int hi = (snakes[o].hist_head - (seg * 4) + HIST_SIZE) % HIST_SIZE;
                    float dx = snakes[o].hist[hi].x - s->pos.x, dy = snakes[o].hist[hi].y - s->pos.y;
                    if ((dx * dx + dy * dy) < (hd * hd)) { kill_snake(i); break; }
                }
                if (!s->alive) break;
            }
        }

        if (snakes[0].alive) {
            cam_x += (snakes[0].pos.x - cam_x) * 0.15f;
            cam_y += (snakes[0].pos.y - cam_y) * 0.15f;
        }
        float tz = 1.15f - (snakes[0].length * 0.0025f);
        if (tz < 0.65f) tz = 0.65f;
        cam_zoom += (tz - cam_zoom) * 0.05f;

        tiny3d_Clear(0xFF161C22, TINY3D_CLEAR_ALL);
        tiny3d_Project2D();
        tiny3d_AlphaTest(0, 0, TINY3D_ALPHA_FUNC_GEQUAL);
        tiny3d_SetBlendMode(TINY3D_BLEND_FUNC_SRC_ALPHA, TINY3D_BLEND_FUNC_ONE_MINUS_SRC_ALPHA);

        float hw = VIRTUAL_W * 0.5f, hh = VIRTUAL_H * 0.5f;
        float vw = VIRTUAL_W / cam_zoom, vh = VIRTUAL_H / cam_zoom;
        draw_quad(0, 0, VIRTUAL_W, VIRTUAL_H, (cam_x - vw * 0.5f) / 256.0f, (cam_y - vh * 0.5f) / 256.0f, (cam_x + vw * 0.5f) / 256.0f, (cam_y + vh * 0.5f) / 256.0f, 0xFFFFFFFF, off_bg, 256, 256);

        for (int f = 0; f < NUM_FOODS; f++) {
            if (!foods[f].active) continue;
            foods[f].pulse += 0.08f;
            float sx = (foods[f].pos.x - cam_x) * cam_zoom + hw, sy = (foods[f].pos.y - cam_y) * cam_zoom + hh;
            if (sx < -30 || sx > VIRTUAL_W + 30 || sy < -30 || sy > VIRTUAL_H + 30) continue;
            float d = foods[f].radius * 2.0f * cam_zoom * (1.0f + sinf(foods[f].pulse) * 0.18f);
            draw_glow(sx, sy, d * 3.0f, foods[f].color);
            draw_circle(sx, sy, d, foods[f].color);
            draw_circle(sx, sy, d * 0.45f, 0xFFFFFFFF);
        }

        for (int i = NUM_SNAKES - 1; i >= 0; i--) {
            Snake *s = &snakes[i];
            if (!s->alive) continue;
            float rad = 10.0f + (s->length * 0.08f);
            if (rad > 24.0f) rad = 24.0f;
            float diam = rad * 2.0f * cam_zoom;

            for (int seg = s->length - 1; seg >= 0; seg--) {
                int hi = (s->hist_head - (seg * 4) + HIST_SIZE) % HIST_SIZE;
                float sx = (s->hist[hi].x - cam_x) * cam_zoom + hw, sy = (s->hist[hi].y - cam_y) * cam_zoom + hh;
                if (sx < -40 || sx > VIRTUAL_W + 40 || sy < -40 || sy > VIRTUAL_H + 40) continue;
                draw_circle(sx + 3.0f, sy + 4.0f, diam, 0x55000000);
                if (s->boost && seg % 2 == 0) draw_glow(sx, sy, diam * 2.1f, s->color);
                draw_circle(sx, sy, diam, (seg % 4 < 2) ? s->color : s->dark_color);
            }

            float hx = (s->pos.x - cam_x) * cam_zoom + hw, hy = (s->pos.y - cam_y) * cam_zoom + hh;
            float r_sc = rad * cam_zoom, eye_d = r_sc * 0.72f, pup_d = eye_d * 0.55f;
            for (int side = -1; side <= 1; side += 2) {
                float eang = s->angle + (0.55f * side);
                float ex = hx + cosf(eang) * (r_sc * 0.52f), ey = hy + sinf(eang) * (r_sc * 0.52f);
                draw_circle(ex, ey, eye_d, 0xFFFFFFFF);
                draw_circle(ex + cosf(s->angle) * (eye_d * 0.2f), ey + sinf(s->angle) * (eye_d * 0.2f), pup_d, 0xFF0A0A0A);
            }
        }

        float mx = VIRTUAL_W - 102.0f, my = 10.0f, ms = 90.0f;
        draw_quad(mx - 2, my - 2, ms + 4, ms + 4, 0, 0, 1, 1, 0xAA000000, off_white, 4, 4);
        draw_quad(mx, my, ms, ms, 0, 0, 1, 1, 0xFF1E2631, off_white, 4, 4);
        for (int i = 0; i < NUM_SNAKES; i++) {
            if (!snakes[i].alive) continue;
            draw_quad(mx + (snakes[i].pos.x / MAP_SIZE) * ms - 1.5f, my + (snakes[i].pos.y / MAP_SIZE) * ms - 1.5f, 3.0f, 3.0f, 0, 0, 1, 1, (i == 0) ? 0xFFFFFFFF : snakes[i].color, off_white, 4, 4);
        }

        if (!snakes[0].alive) {
            draw_quad(hw - 110, hh - 28, 220, 56, 0, 0, 1, 1, 0xCC000000, off_white, 4, 4);
            draw_quad(hw - 110, hh - 28, 220, 3,  0, 0, 1, 1, 0xFFFF4D6D, off_white, 4, 4);
            draw_circle(hw, hh, 20.0f, 0xFF22EE77);
        }

        tiny3d_Flip();
    }
    tiny3d_Exit();
    return 0;
}
