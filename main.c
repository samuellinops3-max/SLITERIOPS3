// Cobra Arena v4 - SEM grade, SEM texto, olho cheio, boost no X
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
#define CB_SIZE (0x100000)
#define HOST_SIZE (32*1024*1024)
#define WORLD 3200.0f
#define MAXSEG 420
#define NSN 10
#define NPEL 500
#define SP 12.0f
#define PI 3.14159265f
typedef struct{float x[MAXSEG],y[MAXSEG];int len,alive,respawn,boost,grow,score,tick;float ang,tgt;u32 col;}Snake;
typedef struct{float x,y;int size,active;u32 col;}Pellet;
static const u32 palette[8]={0x00FF4D4D,0x0055D6FF,0x0066FF66,0x00FFD23F,0x00C77DFF,0x00FF8FAB,0x0000E5C0,0x00FF9F43};
static gcmContextData *ctx;static u32 W,H;static u32 *fb[2];static u32 fboff[2];static u32 *back;static int cur=0,first_flip=1;static volatile int running=1;
static void sysutil_cb(u64 s,u64 p,void*u){(void)p;(void)u;if(s==SYSUTIL_EXIT_GAME)running=0;}
static void init_screen(void){void *host=memalign(1024*1024,HOST_SIZE);rsxInit(&ctx,CB_SIZE,HOST_SIZE,host);videoState st;videoGetState(VIDEO_PRIMARY,0,&st);u32 resid=st.displayMode.resolution;if(videoGetResolutionAvailability(VIDEO_PRIMARY,VIDEO_RESOLUTION_720,VIDEO_ASPECT_AUTO,0))resid=VIDEO_RESOLUTION_720;videoResolution res;videoGetResolution(resid,&res);W=res.width;H=res.height;videoConfiguration vc;memset(&vc,0,sizeof(vc));vc.resolution=resid;vc.format=VIDEO_BUFFER_FORMAT_XRGB;vc.aspect=VIDEO_ASPECT_AUTO;vc.pitch=W*4;videoConfigure(VIDEO_PRIMARY,&vc,NULL,0);videoGetState(VIDEO_PRIMARY,0,&st);gcmSetFlipMode(GCM_FLIP_VSYNC);for(int i=0;i<2;i++){fb[i]=(u32*)rsxMemalign(64,W*H*4);gcmAddressToOffset(fb[i],&fboff[i]);gcmSetDisplayBuffer(i,fboff[i],W*4,W,H);}back=(u32*)memalign(64,W*H*4);memset(back,0,W*H*4);gcmResetFlipStatus();}
static void wait_flip(void){while(gcmGetFlipStatus()!=0)usleep(200);gcmResetFlipStatus();}
static void present(void){if(!first_flip)wait_flip();else gcmResetFlipStatus();memcpy(fb[cur],back,W*H*4);gcmSetFlip(ctx,cur);rsxFlushBuffer(ctx);gcmSetWaitFlip(ctx);cur^=1;first_flip=0;}
static void fill_rect(int x,int y,int w,int h,u32 c){int x0=x<0?0:x,y0=y<0?0:y,x1=x+w,y1=y+h;if(x1>(int)W)x1=W;if(y1>(int)H)y1=H;for(int yy=y0;yy<y1;yy++){u32 *p=back+yy*W;for(int xx=x0;xx<x1;xx++)p[xx]=c;}}
static void circle(int cx,int cy,int r,u32 c){if(r<1)r=1;if(cx+r<0||cx-r>=(int)W||cy+r<0||cy-r>=(int)H)return;for(int dy=-r;dy<=r;dy++){int y=cy+dy;if(y<0||y>=(int)H)continue;int hw=(int)sqrtf((float)(r*r-dy*dy));int x0=cx-hw,x1=cx+hw;if(x0<0)x0=0;if(x1>=(int)W)x1=W-1;u32 *p=back+y*W;for(int x=x0;x<=x1;x++)p[x]=c;}}
static Snake sn[NSN];static Pellet pel[NPEL];static int pelrot=0,dead_t=0,x_prev=0;static float camx=WORLD/2,camy=WORLD/2;static float pad_ax=0,pad_ay=0,pad_mag=0;static int pad_boost=0,pad_x=0,pad_x_pressed=0;
static float frand(void){return (float)rand()/(float)RAND_MAX;}
static float radius_of(const Snake *s){int l=s->len>240?240:s->len;return 13.0f+l*0.035f;}
static void add_pellet(float x,float y,int size){for(int k=0;k<NPEL;k++){int i=(pelrot+k)%NPEL;if(!pel[i].active){pel[i].x=x;pel[i].y=y;pel[i].size=size;pel[i].active=1;pel[i].col=palette[rand()%8];pelrot=(i+1)%NPEL;return;}}}
static void spawn_snake(int i){Snake *s=&sn[i];float x=400+frand()*(WORLD-800),y=400+frand()*(WORLD-800);s->ang=frand()*2*PI;s->tgt=s->ang;s->len=22;for(int j=0;j<s->len;j++){s->x[j]=x-cosf(s->ang)*SP*j;s->y[j]=y-sinf(s->ang)*SP*j;}s->alive=1;s->respawn=0;s->boost=0;s->grow=0;s->tick=0;s->col=palette[i%8];}
static void kill_snake(int i){Snake *s=&sn[i];for(int j=0;j<s->len;j+=2)add_pellet(s->x[j]+(frand()-0.5f)*10,s->y[j]+(frand()-0.5f)*10,2);s->alive=0;s->respawn=240;if(i==0)dead_t=0;}
static void bot_think(int i){Snake *s=&sn[i];float hx=s->x[0],hy=s->y[0];s->tgt+=(frand()-0.5f)*0.25f;if(s->tick%6==0){float best=380*380;int bi=-1;for(int k=0;k<NPEL;k++){if(!pel[k].active)continue;float dx=pel[k].x-hx,dy=pel[k].y-hy,d=dx*dx+dy*dy;if(d<best){best=d;bi=k;}}if(bi>=0)s->tgt=atan2f(pel[bi].y-hy,pel[bi].x-hx);}if(hx<250||hx>WORLD-250||hy<250||hy>WORLD-250)s->tgt=atan2f(WORLD/2-hy,WORLD/2-hx);float px=hx+cosf(s->ang)*55,py=hy+sinf(s->ang)*55;float rs=radius_of(s);for(int o=0;o<NSN;o++){if(o==i||!sn[o].alive)continue;float lim=rs+radius_of(&sn[o])+25;lim*=lim;for(int j=0;j<sn[o].len;j+=2){float dx=sn[o].x[j]-px,dy=sn[o].y[j]-py;if(dx*dx+dy*dy<lim){s->tgt=s->ang+1.6f;return;}}}}
static void update_snake(int i){
Snake *s=&sn[i];if(!s->alive){if(i>0&&--s->respawn<=0)spawn_snake(i);return;}s->tick++;
if(i==0){if(pad_mag>0.3f)s->tgt=atan2f(pad_ay,pad_ax);s->boost=pad_boost;}else{bot_think(i);s->boost=0;}
float d=s->tgt-s->ang;while(d>PI)d-=2*PI;while(d<-PI)d+=2*PI;if(d>0.12f)d=0.12f;if(d<-0.12f)d=-0.12f;s->ang+=d;
float sp=3.4f;if(s->boost&&s->len>12){sp=7.2f;if(s->tick%8==0){s->len--;add_pellet(s->x[s->len],s->y[s->len],1);}}
float nx=s->x[0]+cosf(s->ang)*sp;float ny=s->y[0]+sinf(s->ang)*sp;
for(int j=s->len-1;j>0;j--){s->x[j]=s->x[j-1];s->y[j]=s->y[j-1];}
s->x[0]=nx;s->y[0]=ny;
if(s->x[0]<0||s->x[0]>WORLD||s->y[0]<0||s->y[0]>WORLD){kill_snake(i);return;}
float rs=radius_of(s),er=rs+14;for(int k=0;k<NPEL;k++){if(!pel[k].active)continue;float dx=pel[k].x-s->x[0],dy=pel[k].y-s->y[0];if(dx*dx+dy*dy<er*er){pel[k].active=0;s->grow+=pel[k].size;s->score+=pel[k].size*5;}}
while(s->grow>=2&&s->len<MAXSEG){s->x[s->len]=s->x[s->len-1];s->y[s->len]=s->y[s->len-1];s->len++;s->grow-=2;}if(s->len>=MAXSEG)s->grow=0;
for(int o=0;o<NSN;o++){if(o==i||!sn[o].alive)continue;float lim=(rs+radius_of(&sn[o]))*0.75f;lim*=lim;for(int j=0;j<sn[o].len;j++){float dx=sn[o].x[j]-s->x[0],dy=sn[o].y[j]-s->y[0];if(dx*dx+dy*dy<lim){kill_snake(i);return;}}}}
static void init_game(void){memset(pel,0,sizeof(pel));for(int k=0;k<NPEL;k++)add_pellet(frand()*WORLD,frand()*WORLD,1+(rand()%5==0));for(int i=0;i<NSN;i++){sn[i].score=0;spawn_snake(i);}dead_t=0;}
static void read_pad(void){padInfo info;padData d;pad_ax=pad_ay=pad_mag=0;pad_boost=0;pad_x_pressed=0;ioPadGetInfo(&info);if(info.status[0]){ioPadGetData(0,&d);float ax=((int)d.ANA_L_H-128)/128.0f,ay=((int)d.ANA_L_V-128)/128.0f;if(d.BTN_LEFT)ax=-1;if(d.BTN_RIGHT)ax=1;if(d.BTN_UP)ay=-1;if(d.BTN_DOWN)ay=1;pad_ax=ax;pad_ay=ay;pad_mag=sqrtf(ax*ax+ay*ay);pad_x=d.BTN_CROSS?1:0;pad_boost=(d.BTN_CROSS||d.BTN_R1||d.BTN_R2)?1:0;if(pad_x&&!x_prev)pad_x_pressed=1;x_prev=pad_x;}}
static void update_game(void){for(int i=0;i<NSN;i++)update_snake(i);int count=0;for(int k=0;k<NPEL;k++)count+=pel[k].active;for(int n=0;n<2&&count<NPEL-40;n++,count++)add_pellet(frand()*WORLD,frand()*WORLD,1+(rand()%5==0));if(sn[0].alive){camx=sn[0].x[0];camy=sn[0].y[0];}else{dead_t++;if(dead_t>60&&pad_x_pressed){spawn_snake(0);sn[0].score=0;}}}
static void render(void){
float sc=(float)W/1280.0f;float hw=W/2.0f,hh=H/2.0f;
// FUNDO LISO - sem grade, sem triangulo, sem nada
for(u32 i=0;i<W*H;i++)back[i]=0x0014222E;
// Borda vermelha so nas pontas do mundo
int sx0=(int)((0-camx)*sc+hw),sx1=(int)((WORLD-camx)*sc+hw);int sy0=(int)((0-camy)*sc+hh),sy1=(int)((WORLD-camy)*sc+hh);u32 red=0x00380E12;
if(sx0>0)fill_rect(0,0,sx0,H,red);if(sx1<(int)W)fill_rect(sx1,0,W-sx1,H,red);if(sy0>0)fill_rect(0,0,W,sy0,red);if(sy1<(int)H)fill_rect(0,sy1,W,H-sy1,red);
for(int k=0;k<NPEL;k++){if(!pel[k].active)continue;int px=(int)((pel[k].x-camx)*sc+hw),py=(int)((pel[k].y-camy)*sc+hh);circle(px,py,(int)((6+pel[k].size*3)*sc),pel[k].col);}
for(int i=NSN-1;i>=0;i--){Snake *s=&sn[i];if(!s->alive)continue;int r=(int)(radius_of(s)*sc);u32 dark=(s->col>>1)&0x007F7F7F;
for(int j=s->len-1;j>=0;j--){int px=(int)((s->x[j]-camx)*sc+hw),py=(int)((s->y[j]-camy)*sc+hh);circle(px,py,r,(j%6<3)?s->col:dark);}
// OLHO CHEIO - branco grande + preto no meio, na frente da cabeca
int hx=(int)((s->x[0]-camx)*sc+hw),hy=(int)((s->y[0]-camy)*sc+hh);
for(int e=-1;e<=1;e+=2){int ex=hx+(int)(cosf(s->ang+e*0.65f)*r*0.65f);int ey=hy+(int)(sinf(s->ang+e*0.65f)*r*0.65f);int wr=r*45/100;if(wr<5)wr=5;int pr=r*20/100;if(pr<2)pr=2;circle(ex,ey,wr,0x00FFFFFF);circle(ex+(int)(cosf(s->ang)*wr*0.3f),ey+(int)(sinf(s->ang)*wr*0.3f),pr,0x00000000);}}
// SEM TEXTO NENHUM - nem SCORE, nem X BOOST
}
int main(int argc,char **argv){(void)argc;(void)argv;init_screen();ioPadInit(7);sysUtilRegisterCallback(0,sysutil_cb,NULL);srand(123);init_game();padData pd;memset(&pd,0,sizeof(pd));while(running){sysUtilCheckCallback();read_pad();update_game();render();present();}return 0;}