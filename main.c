#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include <ppu-types.h>
#include <rsx/gcm.h>
#include <rsx/nvreg.h>
#include <io/pad.h>
#include <tiny3d.h>

#define VIRTUAL_WIDTH  848.0f
#define VIRTUAL_HEIGHT 512.0f
#define MAP_WIDTH      4096.0f
#define MAP_HEIGHT     4096.0f

#define MAX_SNAKE_LEN  300
#define NUM_SNAKES     14  // 1 Jogador + 13 Bots
#define NUM_FOODS      400

typedef struct {
    float x, y;
} Vector2;

typedef struct {
    Vector2 pos;
    float angle;
    float speed;
    float target_angle;
    int length;
    Vector2 body[MAX_SNAKE_LEN];
    u32 color;
    int alive;
    int is_bot;
    int ai_timer;
} Snake;

typedef struct {
    Vector2 pos;
    u32 color;
    float size;
    int active;
} Food;

static Snake snakes[NUM_SNAKES];
static Food foods[NUM_FOODS];

static u32 *texture_snake = NULL;
static u32 *texture_food  = NULL;
static u32 *texture_bg    = NULL;

static u32 offset_snake = 0;
static u32 offset_food  = 0;
static u32 offset_bg    = 0;

u32* create_smooth_gradient_texture(u32 base_color) {
    u32 *tex = (u32*)memalign(128, 128 * 128 * 4);
    if (!tex) return NULL;
    
    int x, y;
    float cx = 64.0f, cy = 64.0f, max_r = 64.0f;
    for (y = 0; y < 128; y++) {
        for (x = 0; x < 128; x++) {
            float dx = (float)x - cx;
            float dy = (float)y - cy;
            float dist = sqrtf(dx*dx + dy*dy);
            float alpha = 0.0f;
            if (dist <= max_r) {
                float f = dist / max_r;
                alpha = 1.0f - (f * f); 
                if (alpha < 0.0f) alpha = 0.0f;
            }
            u8 r = (base_color >> 16) & 0xFF;
            u8 g = (base_color >> 8) & 0xFF;
            u8 b = base_color & 0xFF;
            u8 a = (u8)(alpha * 255.0f);
            tex[y * 128 + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    return tex;
}

u32* create_hex_bg_texture() {
    u32 *tex = (u32*)memalign(128, 256 * 256 * 4);
    if (!tex) return NULL;
    int x, y;
    for (y = 0; y < 256; y++) {
        for (x = 0; x < 256; x++) {
            u32 color = 0xFF161C22;
            if ((x % 32 == 0) || (y % 32 == 0)) {
                color = 0xFF1E252E;
            }
            tex[y * 256 + x] = color;
        }
    }
    return tex;
}

void init_game() {
    int i, j;
    
    // Cores seguras com Alpha 0xFF total
    u32 bot_colors[13] = {
        0xFFFF3366, 0xFF33FF99, 0xFF3399FF, 0xFFFFCC00,
        0xFFFF00FF, 0xFF00FFFF, 0xFFFF6600, 0xFF9933FF,
        0xFF66FF33, 0xFFFF3333, 0xFF33FFFF, 0xFFFF99CC, 0xFF99FFCC
    };

    // Jogador
    snakes[0].pos.x = MAP_WIDTH / 2.0f;
    snakes[0].pos.y = MAP_HEIGHT / 2.0f;
    snakes[0].angle = 0.0f;
    snakes[0].speed = 3.5f;
    snakes[0].target_angle = 0.0f;
    snakes[0].length = 30;
    snakes[0].color = 0xFF00FF88;
    snakes[0].alive = 1;
    snakes[0].is_bot = 0;
    
    for (j = 0; j < MAX_SNAKE_LEN; j++) {
        snakes[0].body[j].x = snakes[0].pos.x - (j * 5);
        snakes[0].body[j].y = snakes[0].pos.y;
    }

    // Bots
    for (i = 1; i < NUM_SNAKES; i++) {
        snakes[i].pos.x = 200.0f + (rand() % (int)(MAP_WIDTH - 400.0f));
        snakes[i].pos.y = 200.0f + (rand() % (int)(MAP_HEIGHT - 400.0f));
        snakes[i].angle = (float)(rand() % 628) / 100.0f;
        snakes[i].speed = 3.0f;
        snakes[i].target_angle = snakes[i].angle;
        snakes[i].length = 20 + (rand() % 40);
        snakes[i].color = bot_colors[i - 1];
        snakes[i].alive = 1;
        snakes[i].is_bot = 1;
        snakes[i].ai_timer = 0;

        for (j = 0; j < MAX_SNAKE_LEN; j++) {
            snakes[i].body[j].x = snakes[i].pos.x - (j * 5) * cosf(snakes[i].angle);
            snakes[i].body[j].y = snakes[i].pos.y - (j * 5) * sinf(snakes[i].angle);
        }
    }

    // Comidas
    u32 food_colors[5] = {0xFFFFDD00, 0xFFFF3355, 0xFF33FFCC, 0xFF3399FF, 0xFFFF9933};
    for (i = 0; i < NUM_FOODS; i++) {
        foods[i].pos.x = 100.0f + (rand() % (int)(MAP_WIDTH - 200.0f));
        foods[i].pos.y = 100.0f + (rand() % (int)(MAP_HEIGHT - 200.0f));
        foods[i].color = food_colors[rand() % 5];
        foods[i].size = 5.0f;
        foods[i].active = 1;
    }
}

void draw_quad_textured(float x, float y, float w, float h, u32 color) {
    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexColor(color);
    
    tiny3d_VertexTexture(0.0f, 0.0f);
    tiny3d_VertexPos(x, y, 0.0f);

    tiny3d_VertexTexture(1.0f, 0.0f);
    tiny3d_VertexPos(x + w, y, 0.0f);

    tiny3d_VertexTexture(1.0f, 1.0f);
    tiny3d_VertexPos(x + w, y + h, 0.0f);

    tiny3d_VertexTexture(0.0f, 1.0f);
    tiny3d_VertexPos(x, y + h, 0.0f);
    
    tiny3d_End();
}

void turn_snake_into_food(Snake *s) {
    for (int j = 0; j < s->length; j += 2) {
        for (int i = 0; i < NUM_FOODS; i++) {
            if (!foods[i].active) {
                foods[i].pos = s->body[j];
                foods[i].color = s->color;
                foods[i].active = 1;
                break;
            }
        }
    }
}

int main(int argc, char *argv[]) {
    padInfo padinfo;
    padData paddata;

    tiny3d_Init(1024 * 1024 * 4);
    ioPadInit(6);

    u32 *snake_pixels = create_smooth_gradient_texture(0xFFFFFFFF);
    u32 *food_pixels  = create_smooth_gradient_texture(0xFF00FFFF);
    u32 *bg_pixels    = create_hex_bg_texture();

    // Alocação e obtenção correta dos offsets de textura
    texture_snake = tiny3d_AllocTexture(128 * 128 * 4);
    memcpy(texture_snake, snake_pixels, 128 * 128 * 4);
    offset_snake = tiny3d_TextureOffset(texture_snake);

    texture_food = tiny3d_AllocTexture(128 * 128 * 4);
    memcpy(texture_food, food_pixels, 128 * 128 * 4);
    offset_food = tiny3d_TextureOffset(texture_food);

    texture_bg = tiny3d_AllocTexture(256 * 256 * 4);
    memcpy(texture_bg, bg_pixels, 256 * 256 * 4);
    offset_bg = tiny3d_TextureOffset(texture_bg);

    init_game();

    while (1) {
        ioPadGetInfo(&padinfo);
        for (int i = 0; i < 7; i++) {
            if (padinfo.status[i]) {
                ioPadGetData(i, &paddata);
            }
        }

        // Leitura analógico esquerdo com deadzone 0.38f
        float stick_x = ((float)paddata.ANA_L_H - 128.0f) / 128.0f;
        float stick_y = ((float)paddata.ANA_L_V - 128.0f) / 128.0f;
        float stick_mag = sqrtf(stick_x * stick_x + stick_y * stick_y);

        if (stick_mag > 0.38f) {
            snakes[0].target_angle = atan2f(stick_y, stick_x);
        }

        int turbo = (paddata.BTN_CROSS || (paddata.BTN_R2 > 32));
        float current_speed = turbo ? 5.5f : 3.5f;

        // Atualização Jogador
        if (snakes[0].alive) {
            float angle_diff = snakes[0].target_angle - snakes[0].angle;
            while (angle_diff > M_PI) angle_diff -= 2.0f * M_PI;
            while (angle_diff < -M_PI) angle_diff += 2.0f * M_PI;
            snakes[0].angle += angle_diff * 0.15f;

            snakes[0].pos.x += cosf(snakes[0].angle) * current_speed;
            snakes[0].pos.y += sinf(snakes[0].angle) * current_speed;

            if (snakes[0].pos.x < 50.0f || snakes[0].pos.x > MAP_WIDTH - 50.0f ||
                snakes[0].pos.y < 50.0f || snakes[0].pos.y > MAP_HEIGHT - 50.0f) {
                snakes[0].alive = 0;
            }

            for (int j = snakes[0].length - 1; j > 0; j--) {
                snakes[0].body[j] = snakes[0].body[j - 1];
            }
            snakes[0].body[0] = snakes[0].pos;
        } else {
            if (paddata.BTN_START) init_game();
        }

        // Atualização Bots
        for (int i = 1; i < NUM_SNAKES; i++) {
            if (!snakes[i].alive) continue;
            snakes[i].ai_timer++;
            if (snakes[i].ai_timer > 60) {
                snakes[i].target_angle += ((rand() % 100) - 50.0f) / 50.0f;
                snakes[i].ai_timer = 0;
            }
            float b_diff = snakes[i].target_angle - snakes[i].angle;
            snakes[i].angle += b_diff * 0.1f;
            snakes[i].pos.x += cosf(snakes[i].angle) * snakes[i].speed;
            snakes[i].pos.y += sinf(snakes[i].angle) * snakes[i].speed;

            if (snakes[i].pos.x < 50.0f || snakes[i].pos.x > MAP_WIDTH - 50.0f ||
                snakes[i].pos.y < 50.0f || snakes[i].pos.y > MAP_HEIGHT - 50.0f) {
                snakes[i].target_angle += M_PI;
            }

            for (int j = snakes[i].length - 1; j > 0; j--) {
                snakes[i].body[j] = snakes[i].body[j - 1];
            }
            snakes[i].body[0] = snakes[i].pos;
        }

        // Colisão Cabeça com Comidas
        for (int i = 0; i < NUM_SNAKES; i++) {
            if (!snakes[i].alive) continue;
            for (int f = 0; f < NUM_FOODS; f++) {
                if (foods[f].active) {
                    float dx = snakes[i].pos.x - foods[f].pos.x;
                    float dy = snakes[i].pos.y - foods[f].pos.y;
                    if (sqrtf(dx*dx + dy*dy) < 15.0f) {
                        foods[f].active = 0;
                        if (snakes[i].length < MAX_SNAKE_LEN) snakes[i].length++;
                    }
                }
            }
        }

        // Colisão entre Cobras
        for (int i = 0; i < NUM_SNAKES; i++) {
            if (!snakes[i].alive) continue;
            for (int o = 0; o < NUM_SNAKES; o++) {
                if (i == o || !snakes[o].alive) continue;
                for (int j = 5; j < snakes[o].length; j++) {
                    float dx = snakes[i].pos.x - snakes[o].body[j].x;
                    float dy = snakes[i].pos.y - snakes[o].body[j].y;
                    if (sqrtf(dx*dx + dy*dy) < 10.0f) {
                        snakes[i].alive = 0;
                        turn_snake_into_food(&snakes[i]);
                        break;
                    }
                }
                if (!snakes[i].alive) break;
            }
        }

        // Renderização Tiny3D
        tiny3d_Clear(0xFF161C22, TINY3D_CLEAR_ALL);
        tiny3d_Project2D();

        float cam_x = snakes[0].pos.x - (VIRTUAL_WIDTH / 2.0f);
        float cam_y = snakes[0].pos.y - (VIRTUAL_HEIGHT / 2.0f);

        // Fundo com blend normal
        tiny3d_SetBlendMode(TINY3D_BLEND_FUNC_SRC_ALPHA, TINY3D_BLEND_FUNC_ONE_MINUS_SRC_ALPHA);
        tiny3d_SetTexture(0, offset_bg, 256, 256, 256 * 4, TINY3D_TEX_FORMAT_A8R8G8B8, 1);
        draw_quad_textured(-cam_x * 0.2f, -cam_y * 0.2f, MAP_WIDTH, MAP_HEIGHT, 0xFFFFFFFF);

        // Comidas com brilho neon aditivo
        tiny3d_SetBlendMode(TINY3D_BLEND_FUNC_SRC_ALPHA, TINY3D_BLEND_FUNC_ONE);
        tiny3d_SetTexture(0, offset_food, 128, 128, 128 * 4, TINY3D_TEX_FORMAT_A8R8G8B8, 1);
        for (int i = 0; i < NUM_FOODS; i++) {
            if (foods[i].active) {
                float fx = foods[i].pos.x - cam_x;
                float fy = foods[i].pos.y - cam_y;
                if (fx >= -20 && fx <= VIRTUAL_WIDTH + 20 && fy >= -20 && fy <= VIRTUAL_HEIGHT + 20) {
                    draw_quad_textured(fx - 6, fy - 6, 12, 12, foods[i].color);
                }
            }
        }

        // Cobras (Corpo, Sombra e Olhos)
        tiny3d_SetBlendMode(TINY3D_BLEND_FUNC_SRC_ALPHA, TINY3D_BLEND_FUNC_ONE_MINUS_SRC_ALPHA);
        tiny3d_SetTexture(0, offset_snake, 128, 128, 128 * 4, TINY3D_TEX_FORMAT_A8R8G8B8, 1);
        for (int i = 0; i < NUM_SNAKES; i++) {
            if (!snakes[i].alive) continue;
            
            // Corpo e Sombra
            for (int j = snakes[i].length - 1; j >= 0; j--) {
                float bx = snakes[i].body[j].x - cam_x;
                float by = snakes[i].body[j].y - cam_y;
                if (bx >= -20 && bx <= VIRTUAL_WIDTH + 20 && by >= -20 && by <= VIRTUAL_HEIGHT + 20) {
                    draw_quad_textured(bx - 8 + 2, by - 8 + 2, 16, 16, 0x40000000); // Sombra
                    draw_quad_textured(bx - 8, by - 8, 16, 16, snakes[i].color);
                }
            }

            // Olhos brancos com pupilas na cabeça
            float hx = snakes[i].pos.x - cam_x;
            float hy = snakes[i].pos.y - cam_y;
            float perp = snakes[i].angle + (M_PI / 2.0f);
            float ox1 = hx + cosf(perp) * 6.0f + cosf(snakes[i].angle) * 4.0f;
            float oy1 = hy + sinf(perp) * 6.0f + sinf(snakes[i].angle) * 4.0f;
            float ox2 = hx - cosf(perp) * 6.0f + cosf(snakes[i].angle) * 4.0f;
            float oy2 = hy - sinf(perp) * 6.0f + sinf(snakes[i].angle) * 4.0f;

            draw_quad_textured(ox1 - 3, oy1 - 3, 6, 6, 0xFFFFFFFF);
            draw_quad_textured(ox2 - 3, oy2 - 3, 6, 6, 0xFFFFFFFF);
            draw_quad_textured(ox1 - 1, oy1 - 1, 2, 2, 0xFF000000);
            draw_quad_textured(ox2 - 1, oy2 - 1, 2, 2, 0xFF000000);
        }

        // Minimapa (Canto Superior Direito)
        float mm_x = VIRTUAL_WIDTH - 160.0f;
        float mm_y = 16.0f;
        float mm_w = 144.0f;
        float mm_h = 144.0f;

        tiny3d_SetTexture(0, offset_bg, 256, 256, 256 * 4, TINY3D_TEX_FORMAT_A8R8G8B8, 1);
        draw_quad_textured(mm_x, mm_y, mm_w, mm_h, 0xC00F1318);

        for (int i = 0; i < NUM_SNAKES; i++) {
            if (!snakes[i].alive) continue;
            float px = mm_x + (snakes[i].pos.x / MAP_WIDTH) * mm_w;
            float py = mm_y + (snakes[i].pos.y / MAP_HEIGHT) * mm_h;
            draw_quad_textured(px - 2, py - 2, 4, 4, snakes[i].color);
        }

        tiny3d_Flip();
    }

    return 0;
}
