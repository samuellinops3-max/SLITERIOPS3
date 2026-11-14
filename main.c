// Slither.io PS3 - Tiny3D - main.c - v10 CORRIGIDO
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include <unistd.h>
#include <ppu-types.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <io/pad.h>
#include <tiny3d.h>
#include <rsx/rsx.h>

#define WORLD 5000.0f
#define MAX_SNAKES 14
#define MAX_PELLETS 700
#define HIST 600
#define SEG_SPACING 7.0f
#define MAX_SEG 90
#define PI 3.14159265f

typedef struct { float x, y; } Vec2;
typedef struct {
    Vec2 hist[HIST];
    int histHead;
    float ang, targetAng;
    float speed;
    int len;
    int grow;
    int alive, respawn;
    int boost;
    int score, tick;
    u32 color;
    u32 dark;
    float px, py;
} Snake;

typedef struct { float x, y; int active; int size; u32 color; float pulse; } Pellet;

static volatile int running = 1;
static void sys_cb(u64 s, u64 p, void *u){ (void)p;(void)u; if(s==SYSUTIL_EXIT_GAME) running=0; }

static Snake snakes[MAX_SNAKES];
static Pellet pellets[MAX_PELLETS];
static int pelletCursor = 0;

static float camX = WORLD/2, camY = WORLD/2;
static float camZoom = 1.0f, targetZoom = 1.0f;

// Texturas e Offsets VRAM Tiny3D
static u32 *texCircle = NULL, offCircle = 0;
static u32 *texGlow = NULL, offGlow = 0;
static u32 *texHex = NULL, offHex = 0;
static u32 *texEyeWhite = NULL, offEyeWhite = 0;
static u32 *texPupil = NULL, offPupil = 0;
static u32 *texWhite = NULL, offWhite = 0;

static float frandf(void){ return (float)rand()/(float)RAND_MAX; }
static float clampf(float v,float a,float b){ return v<a?a:(v>b?b:v); }
static float lerpAngle(float a,float b,float t){
    float d=b-a;
    while(d>PI) d-=2*PI;
    while(d<-PI) d+=2*PI;
    return a+d*t;
}
static u32 rgba(u8 r,u8 g,u8 b,u8 a){ return ((u32)a<<24)|((u32)r<<16)|((u32)g<<8)|b; }
static u32 darken(u32 c,float f){
    u8 r=((c>>16)&0xff)*f, g=((c>>8)&0xff)*f, b=(c&0xff)*f;
    return rgba(r,g,b,255);
}
static u32 randomSnakeColor(void){
    int h=rand()%360; float s=0.82f,v=1.0f;
    float cc=v*s; float x=cc*(1-fabsf(fmodf(h/60.0f,2)-1)); float m=v-cc;
    float r=0,g=0,b=0;
    if(h<60){r=cc;g=x;} else if(h<120){r=x;g=cc;} else if(h<180){g=cc;b=x;}
    else if(h<240){g=x;b=cc;} else if(h<300){r=x;b=cc;} else {r=cc;b=x;}
    return rgba((u8)((r+m)*255),(u8)((g+m)*255),(u8)((b+m)*255),255);
}
static u32 randomPelletColor(void){
    static const u32 pal[]={0xFFFF4D6D,0xFFFFD166,0xFF06D6A0,0xFF118AB2,0xFF9B5DE5,0xFFF15BB5,0xFF00F5D4,0xFFFE7A00,0xFF00BBF9,0xFF9BF00B};
    return pal[rand()%10];
}

// --- Construção de Texturas na VRAM (Assinatura oficial tiny3d_AllocTexture) ---
static void buildTextures(void){
    texCircle = (u32 *)tiny3d_AllocTexture(128 * 128 * 4);
    texGlow = (u32 *)tiny3d_AllocTexture(128 * 128 * 4);
    texEyeWhite = (u32 *)tiny3d_AllocTexture(128 * 128 * 4);
    texPupil = (u32 *)tiny3d_AllocTexture(128 * 128 * 4);
    texHex = (u32 *)tiny3d_AllocTexture(256 * 256 * 4);
    texWhite = (u32 *)tiny3d_AllocTexture(4 * 4 * 4);

    for(int i=0;i<16;i++) texWhite[i]=0xFFFFFFFF;

    for(int y=0;y<128;y++) for(int x=0;x<128;x++){
        float dx=(x-63.5f)/63.5f, dy=(y-63.5f)/63.5f;
        float d=sqrtf(dx*dx+dy*dy);

        // Círculo suave AA
        float a=0;
        if(d<1.0f){ float e=(1.0f-d)/0.08f; if(e>1) e=1; a=e*255.0f; }
        texCircle[y*128+x]=rgba(255,255,255,(u8)a);

        // Glow exponencial
        float ag = (d<1.0f) ? expf(-d*d*4.5f)*255.0f : 0;
        texGlow[y*128+x]=rgba(255,255,255,(u8)ag);

        // Olho branco e pupila
        float ae=0; if(d<1.0f){ float e=(1.0f-d)/0.06f; if(e>1) e=1; ae=e*255; }
        texEyeWhite[y*128+x]=rgba(255,255,255,(u8)ae);

        float ap=0; if(d<0.62f){ float e=(0.62f-d)/0.08f; if(e>1) e=1; ap=e*255; }
        texPupil[y*128+x]=rgba(10,10,12,(u8)ap);
    }

    for(int i=0;i<256*256;i++) texHex[i]=0xFF161C22;
    for(int cy=0;cy<256;cy++) for(int cx=0;cx<256;cx++){
        float sx=(float)cx, sy=(float)cy;
        float q = sx*0.8660254f;
        float r = sy - sx*0.5f;
        float gx = fmodf(q, 64.0f); if(gx<0) gx+=64.0f;
        float gy = fmodf(r, 64.0f); if(gy<0) gy+=64.0f;
        float dist = fminf(fminf(gx, 64.0f-gx), fminf(gy, 64.0f-gy));
        if(dist<1.2f){
            float a=1.0f-dist/1.2f;
            u8 v=(u8)(0x2A + a*18);
            texHex[cy*256+cx]=rgba(v, v+6, v+14, 255);
        }
    }

    offCircle = tiny3d_TextureOffset(texCircle);
    offGlow = tiny3d_TextureOffset(texGlow);
    offEyeWhite = tiny3d_TextureOffset(texEyeWhite);
    offPupil = tiny3d_TextureOffset(texPupil);
    offHex = tiny3d_TextureOffset(texHex);
    offWhite = tiny3d_TextureOffset(texWhite);
}

// --- Desenho real usando vértices oficiais da Tiny3D (848x512 virtual) ---
static void drawQuad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, u32 color, u32 texOff, int texW, int texH){
    tiny3d_SetTexture(0, texOff, texW, texH, texW*4, TINY3D_TEX_FORMAT_A8R8G8B8, 1);
    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(x0, y0, 0.0f);
    tiny3d_VertexColor(color);
    tiny3d_VertexTexture(u0, v0);

    tiny3d_VertexPos(x1, y0, 0.0f);
    tiny3d_VertexColor(color);
    tiny3d_VertexTexture(u1, v0);

    tiny3d_VertexPos(x1, y1, 0.0f);
    tiny3d_VertexColor(color);
    tiny3d_VertexTexture(u1, v1);

    tiny3d_VertexPos(x0, y1, 0.0f);
    tiny3d_VertexColor(color);
    tiny3d_VertexTexture(u0, v1);
    tiny3d_End();
}

static void drawQuadCenter(float cx, float cy, float size, u32 color, u32 texOff){
    float hs = size * 0.5f;
    drawQuad(cx - hs, cy - hs, cx + hs, cy + hs, 0.0f, 0.0f, 1.0f, 1.0f, color, texOff, 128, 128);
}

static void drawGlow(float cx, float cy, float size, u32 color){
    tiny3d_SetBlendMode(TINY3D_BLEND_FUNC_SRC_ALPHA, TINY3D_BLEND_FUNC_ONE);
    drawQuadCenter(cx, cy, size, color, offGlow);
    tiny3d_SetBlendMode(TINY3D_BLEND_FUNC_SRC_ALPHA, TINY3D_BLEND_FUNC_ONE_MINUS_SRC_ALPHA);
}

static float radiusOf(Snake *s){ int l=s->len>60?60:s->len; return 11.0f + l*0.22f; }

static void spawnPellet(float x,float y,int size){
    for(int k=0;k<MAX_PELLETS;k++){
        int i=(pelletCursor+k)%MAX_PELLETS;
        if(!pellets[i].active){
            pellets[i].x=x; pellets[i].y=y; pellets[i].size=size; pellets[i].active=1;
            pellets[i].color=randomPelletColor(); pellets[i].pulse=frandf()*6.28f;
            pelletCursor=(i+1)%MAX_PELLETS; return;
        }
    }
}

static void spawnSnake(int idx){
    Snake *s=&snakes[idx];
    float x=400+frandf()*(WORLD-800), y=400+frandf()*(WORLD-800);
    float a=frandf()*2*PI;
    s->ang=a; s->targetAng=a; s->speed=3.1f;
    s->len=14; s->grow=0; s->alive=1; s->respawn=0; s->boost=0; s->tick=0; s->score=0;
    s->color=randomSnakeColor(); s->dark=darken(s->color,0.72f);
    for(int i=0;i<HIST;i++){ s->hist[i].x=x-cosf(a)*SEG_SPACING*i; s->hist[i].y=y-sinf(a)*SEG_SPACING*i; }
    s->histHead=0; s->px=x; s->py=y;
}

static void killSnake(int idx){
    Snake *s=&snakes[idx];
    for(int j=0;j<s->len*3;j+=2){
        int hi=(s->histHead - j*(int)(SEG_SPACING/2) + HIST*4)%HIST;
        Vec2 p=s->hist[hi];
        spawnPellet(p.x+(frandf()-0.5f)*14, p.y+(frandf()-0.5f)*14, 2);
    }
    s->alive=0; s->respawn=180;
}

static float botTx[MAX_SNAKES], botTy[MAX_SNAKES];
static int botTimer[MAX_SNAKES];

static void botThink(int idx){
    Snake *s=&snakes[idx];
    float hx=s->px, hy=s->py;
    botTimer[idx]--;
    float dx=botTx[idx]-hx, dy=botTy[idx]-hy;
    if(botTimer[idx]<=0 || dx*dx+dy*dy<60*60){
        float best=1e12f; int bi=-1;
        for(int k=0;k<MAX_PELLETS;k++){ if(!pellets[k].active) continue;
            float ddx=pellets[k].x-hx, ddy=pellets[k].y-hy; float d=ddx*ddx+ddy*ddy;
            if(d<best){best=d; bi=k;}
        }
        if(bi>=0){ botTx[idx]=pellets[bi].x; botTy[idx]=pellets[bi].y; }
        else { botTx[idx]=500+frandf()*(WORLD-1000); botTy[idx]=500+frandf()*(WORLD-1000); }
        botTimer[idx]=140+rand()%160;
    }
    float target=atan2f(botTy[idx]-hy, botTx[idx]-hx);
    if(hx<380||hx>WORLD-380||hy<380||hy>WORLD-380){ target=atan2f(WORLD/2-hy, WORLD/2-hx); botTimer[idx]=0; }
    float px=hx+cosf(s->ang)*110, py=hy+sinf(s->ang)*110;
    float rs=radiusOf(s);
    for(int o=0;o<MAX_SNAKES;o++){
        if(o==idx||!snakes[o].alive) continue;
        float lim=rs+radiusOf(&snakes[o])+28; lim*=lim;
        for(int j=0;j<snakes[o].len;j+=2){
            int hi=(snakes[o].histHead - j*2 + HIST*8)%HIST;
            float ddx=snakes[o].hist[hi].x-px, ddy=snakes[o].hist[hi].y-py;
            if(ddx*ddx+ddy*ddy<lim){
                float cross=cosf(s->ang)*ddy - sinf(s->ang)*ddx;
                float side=(cross>0)?-1.0f:1.0f;
                target=s->ang+1.05f*side;
                break;
            }
        }
    }
    s->targetAng=target;
}

typedef struct { float ax, ay, mag; int boost; int xPressed; int connected; } PadState;
static PadState padSt;
static int prevX=0;

static void readPad(void){
    padInfo info; padData d;
    memset(&d,0,sizeof(d));
    memset(&padSt,0,sizeof(padSt));
    ioPadGetInfo(&info);
    if(!info.status[0]){ prevX=0; return; }
    padSt.connected=1;
    if(ioPadGetData(0,&d)!=0){ prevX=0; return; }
    float ax=((int)d.ANA_L_H-128)/127.0f;
    float ay=((int)d.ANA_L_V-128)/127.0f;
    if(d.BTN_LEFT) ax=-1;
    if(d.BTN_RIGHT) ax=1;
    if(d.BTN_UP) ay=-1;
    if(d.BTN_DOWN) ay=1;
    if(ax>1) ax=1; if(ax<-1) ax=-1;
    if(ay>1) ay=1; if(ay<-1) ay=-1;
    float mag=sqrtf(ax*ax+ay*ay);
    if(mag<0.38f){ ax=0; ay=0; mag=0; }
    else if(mag>1.0f){ ax/=mag; ay/=mag; mag=1.0f; }
    padSt.ax=ax; padSt.ay=ay; padSt.mag=mag;
    int xHeld = d.BTN_CROSS ? 1 : 0;
    int r2Held = (d.PRE_R2>80 || d.BTN_R2) ? 1 : 0;
    padSt.boost = (xHeld||r2Held) ? 1 : 0;
    padSt.xPressed = (xHeld && !prevX) ? 1 : 0;
    prevX=xHeld;
}

static void updateSnake(int idx){
    Snake *s=&snakes[idx];>py-camY)zoom+hh;
float ex1=cosf(s->ang-0.58f), ey1=sinf(s->ang-0.58f);
float ex2=cosf(s->ang+0.58f), ey2=sinf(s->ang+0.58f);
float eyeR=r0.48f; if(eyeR<4.5f) eyeR=4.5f;
float ox1=sx+ex1r0.55f, oy1=sy+ey1r0.55f;
float ox2=sx+ex2r0.55f, oy2=sy+ey2r0.55f;
drawQuadCenter(ox1,oy1,eyeR2.0f,0xFFFFFFFF,offEyeWhite);
drawQuadCenter(ox2,oy2,eyeR2.0f,0xFFFFFFFF,offEyeWhite);
float px1=ox1+cosf(s->ang)eyeR0.38f, py1=oy1+sinf(s->ang)eyeR0.38f;
float px2=ox2+cosf(s->ang)eyeR0.38f, py2=oy2+sinf(s->ang)eyeR0.38f;
drawQuadCenter(px1,py1,eyeR1.15f,0xFF000000,offPupil);
drawQuadCenter(px2,py2,eyeR1.15f,0xFF000000,offPupil);
drawQuadCenter(px1-eyeR0.2f, py1-eyeR0.2f, eyeR0.45f, 0xFFFFFFFF, offCircle);
drawQuadCenter(px2-eyeR0.2f, py2-eyeR0.2f, eyeR*0.45f, 0xFFFFFFFF, offCircle);
}
}
// 4. Minimapa
{
float mx=W-105, my=10, ms=92;
drawQuad(mx-2, my-2, mx+ms+2, my+ms+2, 0,0,1,1, 0xCC000000, offWhite, 4, 4);
drawQuad(mx, my, mx+ms, my+ms, 0,0,1,1, 0xFF1E2A3A, offWhite, 4, 4);
for(int i=0;i<MAX_SNAKES;i++){
if(!snakes[i].alive) continue;
float dx=mx+snakes[i].px/WORLDms;
float dy=my+snakes[i].py/WORLDms;
u32 c=(i==0)?0xFFFFFFFF:snakes[i].color;
drawQuad(dx-2, dy-2, dx+2, dy+2, 0,0,1,1, c, offWhite, 4, 4);
}
}
// 5. Painel de Game Over (Pressione X)
if(!snakes[0].alive){
drawQuad(W/2-130, H/2-35, W/2+130, H/2+35, 0,0,1,1, 0xCC000000, offWhite, 4, 4);
drawQuad(W/2-130, H/2-35, W/2+130, H/2-33, 0,0,1,1, 0xFFFF4D6D, offWhite, 4, 4);
float pulse=0.65f+0.35fsinf(snakes[0].tick0.15f);
u8 alpha=(u8)(pulse*255);
drawQuadCenter(W/2, H/2, 24.0f, rgba(255,255,255,alpha), offCircle);
}
tiny3d_Flip();
}
int main(int argc, const char *argv[]) {
(void)argc; (void)argv;
tiny3d_Init(1024 * 1024);
ioPadInit(7);
sysUtilRegisterCallback(0, sys_cb, NULL);
buildTextures();
initGame();
while(running) {
sysUtilCheckCallback();
readPad();
updateGame();
render();
}
tiny3d_Exit();
return 0;
}