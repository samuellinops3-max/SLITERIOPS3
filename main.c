#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL/SDL.h>

#ifdef __lv2ppu__
#include <sysutil/sysutil.h>
#endif

#define W 1280
#define H 720
#define WORLD_W 4200.0f
#define WORLD_H 3000.0f
#define SNAKES 16
#define SEGS 96
#define ORBS 180
#define PI 3.14159265358979323846f

typedef struct {
    float x, y, r;
    Uint32 color;
    int active, pulse;
} Orb;

typedef struct {
    float x[SEGS], y[SEGS];
    float angle, speed, size;
    Uint32 color, stripe;
    int alive, bot, boost, score, tick;
} Snake;

static SDL_Surface *screen;
static Uint32 *pixels;
static int pitch_pixels;

static Orb orbs[ORBS];
static Snake snakes[SNAKES];
static SDL_Joystick *pad;

static float cam_x = WORLD_W * 0.5f;
static float cam_y = WORLD_H * 0.5f;
static float cam_zoom = 1.0f;

static unsigned int frame_no = 0;
static int running = 1;
static int game_over = 0;

static Uint32 bg = 0xFF161C22u;

static const Uint32 palette[8] = {
    0xFF45D6FFu, 0xFFFFB84Du,
    0xFF9C7BFFu, 0xFF62E58Bu,
    0xFFFF6688u, 0xFFE8E86Au,
    0xFF58A6FFu, 0xFFFF8CE8u
};

static const unsigned char font5x7[37][7] = {
 {14,17,19,21,25,17,14},
 {4,12,4,4,4,4,14},
 {14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},
 {2,6,10,18,31,2,2},
 {31,16,16,30,1,1,30},
 {6,8,16,30,17,17,14},
 {31,1,2,4,8,8,8},
 {14,17,17,14,17,17,14},
 {14,17,17,15,1,2,12},
 {14,17,17,31,17,17,17},
 {30,17,17,30,17,17,30},
 {14,17,16,16,16,17,14},
 {28,18,17,17,17,18,28},
 {31,16,16,30,16,16,31},
 {31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},
 {17,17,17,31,17,17,17},
 {14,4,4,4,4,4,14},
 {7,2,2,2,18,18,12},
 {17,18,20,24,20,18,17},
 {16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},
 {17,25,21,19,17,17,17},
 {14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},
 {14,17,17,17,21,18,13},
 {30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},
 {31,4,4,4,4,4,4},
 {17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},
 {17,17,17,21,21,21,10},
 {17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},
 {31,1,2,4,8,16,31},
 {0,0,0,0,0,0,0}
};

static void put_pixel(int x, int y, Uint32 c) {
    if ((unsigned)x < W && (unsigned)y < H)
        pixels[y * pitch_pixels + x] = c;
}

static void clear_screen(Uint32 c) {
    int x, y;

    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            pixels[y * pitch_pixels + x] = c;
}

static void fill_rect(int x, int y, int w, int h, Uint32 c) {
    int xx, yy;

    for (yy = y; yy < y + h; yy++)
        for (xx = x; xx < x + w; xx++)
            put_pixel(xx, yy, c);
}

static void circle(int cx, int cy, int r, Uint32 c) {
    int x, y;

    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x*x + y*y <= r*r)
                put_pixel(cx+x, cy+y, c);
}

static void ring(int cx, int cy, int r, int thick, Uint32 c) {
    int x, y;

    for (y = -r; y <= r; y++) {
        for (x = -r; x <= r; x++) {
            int d = x*x + y*y;

            if (d <= r*r && d >= (r-thick)*(r-thick))
                put_pixel(cx+x, cy+y, c);
        }
    }
}

static void line(int x0, int y0, int x1, int y1, Uint32 c) {
    int dx = abs(x1-x0);
    int sx = x0<x1 ? 1 : -1;
    int dy = -abs(y1-y0);
    int sy = y0<y1 ? 1 : -1;
    int err = dx+dy;

    for (;;) {
        put_pixel(x0,y0,c);

        if (x0==x1 && y0==y1)
            break;

        int e2 = 2*err;

        if (e2>=dy) {
            err+=dy;
            x0+=sx;
        }

        if (e2<=dx) {
            err+=dx;
            y0+=sy;
        }
    }
}

static int glyph_index(char ch) {
    if (ch >= '0' && ch <= '9')
        return ch-'0';

    if (ch >= 'A' && ch <= 'Z')
        return 10+ch-'A';

    return 36;
}

static void draw_text(int x, int y, const char *s,
                      int scale, Uint32 color) {
    while (*s) {
        int g = glyph_index(*s++);
        int row, col;

        for (row=0; row<7; row++) {
            for (col=0; col<5; col++) {
                if (font5x7[g][row] & (1u << (4-col)))
                    fill_rect(
                        x+col*scale,
                        y+row*scale,
                        scale,
                        scale,
                        color
                    );
            }
        }

        x += 6*scale;
    }
}

static float clampf(float v, float lo, float hi) {
    return v<lo ? lo : (v>hi ? hi : v);
}

static float wrap_angle(float a) {
    while (a > PI) a -= 2.0f*PI;
    while (a < -PI) a += 2.0f*PI;
    return a;
}

static int sx(float wx) {
    return (int)((wx-cam_x)*cam_zoom+W*0.5f);
}

static int sy(float wy) {
    return (int)((wy-cam_y)*cam_zoom+H*0.5f);
}

static void spawn_orbs(void) {
    int i;

    for (i=0; i<ORBS; i++) {
        orbs[i].x = 120.0f+(float)((i*197+71)%3960);
        orbs[i].y = 120.0f+(float)((i*313+29)%2760);
        orbs[i].r = 4.0f+(float)(i%5);
        orbs[i].color = palette[i%8];
        orbs[i].active = 1;
        orbs[i].pulse = i*7;
    }
}

static void init_snake(Snake *s, int index,
                       float x, float y, int bot) {
    int j;

    memset(s,0,sizeof(*s));

    s->alive=1;
    s->bot=bot;
    s->x[0]=x;
    s->y[0]=y;
    s->angle=(float)(index*37)*PI/180.0f;
    s->speed=2.25f;
    s->size=12.0f;
    s->color=palette[index%8];

    for (j=1; j<SEGS; j++) {
        s->x[j]=x-cosf(s->angle)*j*5.0f;
        s->y[j]=y-sinf(s->angle)*j*5.0f;
    }
}

static void reset_game(void) {
    int i;

    for (i=0; i<SNAKES; i++)
        init_snake(
            &snakes[i],
            i,
            600.0f+(float)((i*431)%3000),
            500.0f+(float)((i*277)%1900),
            i!=0
        );

    spawn_orbs();

    cam_x=snakes[0].x[0];
    cam_y=snakes[0].y[0];
    cam_zoom=1.0f;
    game_over=0;
}

static void draw_world_grid(void) {
    int x,y;
    const float spacing=84.0f;

    float startx=floorf(
        (cam_x-W/(2*cam_zoom))/spacing
    )*spacing;

    float starty=floorf(
        (cam_y-H/(2*cam_zoom))/spacing
    )*spacing;

    for (y=0; y<48; y++) {
        for (x=0; x<64; x++) {
            float wx=startx+x*spacing;
            float wy=starty+y*spacing;

            int px=sx(wx);
            int py=sy(wy);
            int r=(int)(24*cam_zoom);
            int k;

            for (k=0; k<6; k++) {
                float a=k*PI/3.0f;
                float b=(k+1)*PI/3.0f;

                line(
                    px+(int)(cosf(a)*r),
                    py+(int)(sinf(a)*r),
                    px+(int)(cosf(b)*r),
                    py+(int)(sinf(b)*r),
                    0xFF222B33u
                );
            }
        }
    }
}

static void draw_orbs(void) {
    int i;

    for (i=0; i<ORBS; i++) {
        if (orbs[i].active) {
            int pulse=(int)(
                2.0f*sinf((float)(frame_no+orbs[i].pulse)*0.08f)
            );

            int r=(int)((orbs[i].r+pulse)*cam_zoom);

            if (r<2) r=2;

            circle(sx(orbs[i].x),sy(orbs[i].y),
                   r,orbs[i].color);
        }
    }
}

static void draw_border(void) {
    int left=sx(0);
    int right=sx(WORLD_W);
    int top=sy(0);
    int bottom=sy(WORLD_H);

    int t=(int)(12*cam_zoom);

    if (t<2) t=2;

    fill_rect(left,top,right-left,t,0xFFFF354Fu);
    fill_rect(left,bottom-t,right-left,t,0xFFFF354Fu);
    fill_rect(left,top,t,bottom-top,0xFFFF354Fu);
    fill_rect(right-t,top,t,bottom-top,0xFFFF354Fu);
}

static void draw_snakes(void) {
    int i,j;

    for (i=0; i<SNAKES; i++) {
        if (!snakes[i].alive)
            continue;

        Snake *s=&snakes[i];

        for (j=SEGS-1; j>=0; j--) {
            int r=(int)(
                s->size*
                (1.0f-(float)j/(float)(SEGS*2))*
                cam_zoom
            );

            if (r<2) r=2;

            circle(sx(s->x[j]),sy(s->y[j]),r+1,0xFF0B1117u);
            circle(sx(s->x[j]),sy(s->y[j]),r,s->color);
        }

        int hx=sx(s->x[0]);
        int hy=sy(s->y[0]);

        float a=s->angle;

        int ex=hx+(int)(cosf(a)*s->size*0.55f*cam_zoom);
        int ey=hy+(int)(sinf(a)*s->size*0.55f*cam_zoom);

        int ox=(int)(-sinf(a)*s->size*0.45f*cam_zoom);
        int oy=(int)(cosf(a)*s->size*0.45f*cam_zoom);

        circle(ex+ox,ey+oy,5,0xFFFFFFFFu);
        circle(ex-ox,ey-oy,5,0xFFFFFFFFu);

        circle(ex+ox,ey+oy,2,0xFF101820u);
        circle(ex-ox,ey-oy,2,0xFF101820u);

        if (s->boost)
            ring(hx,hy,(int)(s->size*1.45f*cam_zoom),
                 2,0xFFFFFFFFu);
    }
}

static void draw_minimap(void) {
    int cx=1120, cy=570, r=78, i;

    circle(cx,cy,r,0xCC0A1118u);
    ring(cx,cy,r,3,0xFF71808Cu);

    for (i=0; i<SNAKES; i++) {
        if (snakes[i].alive) {
            int px=cx+(int)(
                (snakes[i].x[0]/WORLD_W-0.5f)*r*1.65f
            );

            int py=cy+(int)(
                (snakes[i].y[0]/WORLD_H-0.5f)*r*1.65f
            );

            circle(px,py,i==0?5:3,snakes[i].color);
        }
    }
}

static void draw_hud(void) {
    char buf[64];

    fill_rect(18,18,260,84,0xAA081018u);

    draw_text(32,30,"NEON COIL ARENA",2,0xFFFFFFFFu);

    sprintf(buf,"SIZE %d",(int)snakes[0].size);
    draw_text(32,54,buf,2,0xFF71E7FFu);

    sprintf(buf,"SCORE %d",snakes[0].score);
    draw_text(32,78,buf,1,0xFFFFFFFFu);

    draw_minimap();
}

static void draw_game_over(void) {
    fill_rect(0,0,W,H,0x99000000u);
    fill_rect(390,260,500,180,0xFF111B24u);

    draw_text(520,292,"GAME OVER",4,0xFFFF6478u);
    draw_text(500,365,"PRESS X TO RESTART",2,0xFFFFFFFFu);
}

static void update_snake(Snake *s,float turn,int boost) {
    int j;

    float turn_limit=0.055f/(1.0f+s->size*0.018f);

    s->angle=wrap_angle(
        s->angle+clampf(turn,-1.0f,1.0f)*turn_limit
    );

    s->boost=boost && s->size>8.0f;
    s->speed=s->boost?3.65f:2.25f;

    for (j=SEGS-1; j>0; j--) {
        s->x[j]=s->x[j-1];
        s->y[j]=s->y[j-1];
    }

    s->x[0]+=cosf(s->angle)*s->speed;
    s->y[0]+=sinf(s->angle)*s->speed;

    if (s->boost && frame_no%8u==0u && s->size>9.0f) {
        int k;

        for (k=0; k<ORBS; k++) {
            if (!orbs[k].active) {
                orbs[k].x=s->x[SEGS-1];
                orbs[k].y=s->y[SEGS-1];
                orbs[k].r=5.0f;
                orbs[k].color=s->color;
                orbs[k].active=1;

                s->size-=0.08f;
                break;
            }
        }
    }
}

static void update_bots(void) {
    int i;

    for (i=1; i<SNAKES; i++) {
        if (!snakes[i].alive)
            continue;

        Snake *s=&snakes[i];

        float best=1e20f;
        float target=s->angle;
        float turn;

        int k;

        for (k=0; k<ORBS; k++) {
            if (orbs[k].active) {
                float dx=orbs[k].x-s->x[0];
                float dy=orbs[k].y-s->y[0];
                float d=dx*dx+dy*dy;

                if (d<best) {
                    best=d;
                    target=atan2f(dy,dx);
                }
            }
        }

        if (s->x[0]<260) target=0.0f;
        if (s->x[0]>WORLD_W-260) target=PI;
        if (s->y[0]<260) target=PI*0.5f;
        if (s->y[0]>WORLD_H-260) target=-PI*0.5f;

        turn=clampf(
            wrap_angle(target-s->angle)*4.0f,
            -1.0f,1.0f
        );

        update_snake(s,turn,0);
    }
}

static void collect_and_collide(void) {
    int i,j,k;

    for (i=0; i<SNAKES; i++) {
        if (!snakes[i].alive)
            continue;

        Snake *s=&snakes[i];

        if (s->x[0]<30 || s->y[0]<30 ||
            s->x[0]>WORLD_W-30 ||
            s->y[0]>WORLD_H-30) {

            if (i==0)
                game_over=1;
            else
                s->alive=0;
        }

        for (k=0; k<ORBS; k++) {
            if (orbs[k].active) {
                float dx=s->x[0]-orbs[k].x;
                float dy=s->y[0]-orbs[k].y;

                float d=dx*dx+dy*dy;
                float reach=s->size+orbs[k].r+5;

                if (d<reach*reach) {
                    orbs[k].active=0;
                    s->score+=10;
                    s->size=clampf(s->size+0.12f,8.0f,28.0f);
                }
            }
        }

        for (j=0; j<SNAKES; j++) {
            if (j==i || !snakes[j].alive)
                continue;

            int q;

            for (q=3; q<SEGS; q+=4) {
                float dx=s->x[0]-snakes[j].x[q];
                float dy=s->y[0]-snakes[j].y[q];
                float rr=s->size*0.8f;

                if (dx*dx+dy*dy<rr*rr) {
                    if (i==0)
                        game_over=1;
                    else
                        s->alive=0;

                    break;
                }
            }
        }
    }

    for (i=0; i<ORBS; i++) {
        if (!orbs[i].active && frame_no%7u==0u) {
            orbs[i].x=100.0f+(float)((i*251+(int)frame_no*3)%4000);
            orbs[i].y=100.0f+(float)((i*173+(int)frame_no*5)%2800);
            orbs[i].active=1;
        }
    }
}

static void read_controls(float *turn,int *boost,int *restart) {
    Sint16 ax=0;

    *turn=0.0f;
    *boost=0;
    *restart=0;

    if (pad) {
        ax=SDL_JoystickGetAxis(pad,0);

        float v=(float)ax/32768.0f;
        float a=fabsf(v);

        if (a>0.15f) {
            float n=(a-0.15f)/0.85f;
            *turn=(v<0?-1.0f:1.0f)*n*n;
        }

        if (SDL_JoystickGetButton(pad,0) ||
            SDL_JoystickGetButton(pad,14) ||
            SDL_JoystickGetButton(pad,9))
            *boost=1;

        if (SDL_JoystickGetButton(pad,0) ||
            SDL_JoystickGetButton(pad,14))
            *restart=1;
    }

    Uint8 *keys=SDL_GetKeyState(NULL);

    if (keys[SDLK_LEFT]) *turn=-1.0f;
    if (keys[SDLK_RIGHT]) *turn=1.0f;
    if (keys[SDLK_SPACE]) *boost=1;
    if (keys[SDLK_RETURN]) *restart=1;
}

int main(int argc,char **argv) {
    (void)argc;
    (void)argv;

    if (SDL_Init(
        SDL_INIT_VIDEO |
        SDL_INIT_JOYSTICK |
        SDL_INIT_NOPARACHUTE
    )<0)
        return 1;

    screen=SDL_SetVideoMode(
        1280,720,32,SDL_SWSURFACE
    );

    if (!screen) {
        SDL_Quit();
        return 2;
    }

    SDL_WM_SetCaption(
        "Neon Coil Arena",
        "Neon Coil Arena"
    );

    if (SDL_NumJoysticks()>0)
        pad=SDL_JoystickOpen(0);

    pitch_pixels=screen->pitch/sizeof(Uint32);
    pixels=(Uint32*)screen->pixels;

    reset_game();

    while (running) {
        SDL_Event event;

        float turn=0.0f;
        int boost=0;
        int restart=0;

        while (SDL_PollEvent(&event)) {
            if (event.type==SDL_QUIT)
                running=0;
        }

#ifdef __lv2ppu__
        sysUtilCheckCallback();
#endif

        read_controls(&turn,&boost,&restart);

        if (game_over) {
            if (restart)
                reset_game();
        } else {
            update_snake(&snakes[0],turn,boost);
            update_bots();
            collect_and_collide();
        }

        cam_x+=(snakes[0].x[0]-cam_x)*0.08f;
        cam_y+=(snakes[0].y[0]-cam_y)*0.08f;

        cam_zoom=clampf(
            1.08f-(snakes[0].size-12.0f)*0.012f,
            0.62f,1.08f
        );

        if (SDL_MUSTLOCK(screen))
            SDL_LockSurface(screen);

        pixels=(Uint32*)screen->pixels;

        clear_screen(bg);
        draw_world_grid();
        draw_orbs();
        draw_border();
        draw_snakes();
        draw_hud();

        if (game_over)
            draw_game_over();

        if (SDL_MUSTLOCK(screen))
            SDL_UnlockSurface(screen);

        SDL_Flip(screen);

        frame_no++;

        SDL_Delay(16);
    }

    if (pad)
        SDL_JoystickClose(pad);

    SDL_Quit();

    return 0;
}