// Cobra Arena v7 - tudo arrumado, compila direto
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
#define NSN 12
#define NPEL 450
#define SP 12.0f
#define PI 3.14159265f
typedef struct{float x[MAXSEG],y[MAXSEG];int len,alive,respawn,boost,grow,score,tick;float ang,tgt;u32 col;}Snake;
typedef struct{float x,y;int size,active;u32 col;}Pellet;
static gcmContextData *ctx;static u32 W,H;static u32 *fb[2];static u32 fboff[2];static u32 *back;static int cur=0,first_flip=1;static volatile int running=1;
static void sysutil_cb(u64 s,u64 p,void*u){(void)p;(void)u;if(s==SYSUTIL_EXIT_GAME)running=0;}
static void init_screen(void){void *host=memalign(1024*1024,HOST_SIZE);rsxInit(&ctx,CB_SIZE,HOST_SIZE,host);videoState st;videoGetState(VIDEO_PRIMARY,0,&st);u32 resid=st.displayMode.resolution;if(videoGetResolutionAvailability(VIDEO_PRIMARY,VIDEO_RESOLUTION_720,VIDEO_ASPECT_AUTO,0))resid=VIDEO_RESOLUTION_720;videoResolution res;videoGetResolution(resid,&res);W=res.width;H=res.height;videoConfiguration vc;memset(&vc,0,sizeof(vc));vc.resolution=resid;vc.format=VIDEO_BUFFER_FORMAT_XRGB;vc.aspect=VIDEO_ASPECT_AUTO;vc.pitch=W*4;videoConfigure(VIDEO_PRIMARY,&vc,NULL,0);videoGetState(VIDEO_PRIMARY,0,&st);gcmSetFlipMode(GCM_FLIP_VSYNC);for(int i=0;i<2;i++){fb[i]=(u32*)rsxMemalign(64,W*H*4);gcmAddressToOffset(fb[i],&fboff[i]);gcmSetDisplayBuffer(i,fboff[i],W*4,W,H);}back=(u32*)memalign(64,W*H*4);memset(back,0,W*H*4);gcmResetFlipStatus();}
static void wait_flip(void){while(gcmGetFlipStatus()!=0)usleep(200);gcmResetFlipStatus();}
static void present(void){if(!first_flip)wait_flip();else gcmResetFlipStatus();memcpy(fb[cur],back,W*H*4);gcmSetFlip(ctx,cur);rsxFlushBuffer(ctx);gcmSetWaitFlip(ctx);cur^=1;first_flip=0;}
static void fill_rect(int x,int y,int w,int h,u32 c){int x0=x<0?0:x,y0=y<0?0:y,x1=x+w,y1=y+h;if(x1>(int)W)x1=W;if(y1>(int)H)y1=H;for(int yy=y0;yy<y1;yy++){u32 *p=back+yy*W;for(int xx=x0;xx<x1;xx++)p[xx]=c;}}
static void circle(int cx,int cy,int r,u32 c){if(r<1)r=1;if(cx+r<0||cx-r>=(int)W||cy+r<0||cy-r>=(int)H)return;for(int dy=-r;dy<=r;dy++){int y=cy+dy;if(y<0||y>=(int)H)continue;int hw=(int)sqrtf((float)(r*r-dy*dy));int x0=cx-hw,x1=cx+hw;if(x0<0)x0=0;if(x1>=(int)W)x1=W-1;u32 *p=back+y*W;for(int x=x0;x<=x1;x++)p[x]=c;}}
typedef struct{char c;const char *p;}Glyph;
static const Glyph glyphs[]={
{'A',".#." "#.#" "###" "#.#" "#.#"},
{'P',"###" "#.#" "###" "#.." "#.."},
{'E',"###" "#.." "###" "#.." "###"},
{'R',"##." "#.#" "##." "#.#" "#.#"},
{'T',"###" ".#." ".#." ".#." ".#."},
{'X',"#.#" "#.#" ".#." "#.#" "#.#"},
{' ',"   " "   " "   " "   " "   "}};
static const char *find_glyph(char c){for(unsigned i=0;i<sizeof(glyphs)/sizeof(glyphs[0]);i++)if(glyphs[i].c==c)return glyphs[i].p;return glyphs[6].p;}
static void draw_text(const char *s,int x,int y,int sc,u32 c){for(;*s;s++){const char *g=find_glyph(*s);for(int r=0;r<5;r++)for(int k=0;k<3;k++)if(g[r*3+k]=='#')fill_rect(x+k*sc,y+r*sc,sc,sc,c);x+=4*sc;}}
static Snake sn[NSN];static Pellet pel[NPEL];static int pelrot=0,dead_t=0,x_prev=0;static float camx=WORLD/2,camy=WORLD/2;static float pad_ax=0,pad_ay=0,pad_mag=0;static int pad_boost=0,pad_x=0,pad_x_pressed=0;
static float bot_tx[NSN],bot_ty[NSN];static int bot_timer[NSN],bot_side[NSN];static float bot_lastx[NSN],bot_lasty[NSN];static int bot_stuck[NSN];
static float frand(void){return (float)rand()/(float)RAND_MAX;}
static u32 random_color(void){int h=rand()%360;float s=0.85f,v=1.0f;float c=v*s;float x=c*(1-fabsf(fmodf(h/60.0f,2)-1));float m=v-c;float r=0,g=0,b=0;if(h<60){r=c;g=x;}else if(h<120){r=x;g=c;}else if(h<180){g=c;b=x;}else if(h<240){g=x;b=c;}else if(h<300){r=x;b=c;}else{r=c;b=x;}return ((u32)((r+m)*255)<<16)|((u32)((g+m)*255)<<8)|((u32)((b+m)*255));}
static float radius_of(const Snake *s){int l=s->len>240?240:s->len;return 13.0f+l*0.035f;}
static void add_pellet(float x,float y,int size){for(int k=0;k<NPEL;k++){int i=(pelrot+k)%NPEL;if(!pel[i].active){pel[i].x=x;pel[i].y=y;pel[i].size=size;pel[i].active=1;pel[i].col=random_color();pelrot=(i+1)%NPEL;return;}}}
static void spawn_snake(int i){Snake *s=&sn[i];float x=400+frand()*(WORLD-800),y=400+frand()*(WORLD-800);s->ang=frand()*2*PI;s->tgt=s->ang;s->len=22;for(int j=0;j<s->len;j++){s->x[j]=x-cosf(s->ang)*SP*j;s->y[j]=y-sinf(s->ang)*SP*j;}s->alive=1;s->respawn=0;s->boost=0;s->grow=0;s->tick=0;s->col=random_color();bot_tx[i]=x+300;bot_ty[i]=y;bot_timer[i]=0;bot_side[i]=(rand()%2)?1:-1;bot_lastx[i]=x;bot_lasty[i]=y;bot_stuck[i]=0;}
static void kill_snake(int i){Snake *s=&sn[i];for(int j=0;j<s->len;j+=2)add_pellet(s->x[j]+(frand()-0.5f)*10,s->y[j]+(frand()-0.5f)*10,2);s->alive=0;s->respawn=240;if(i==0)dead_t=0;}
static void bot_think(int i){
Snake *s=&sn[i];float hx=s->x[0],hy=s->y[0];
if(s->tick%60==0){float dx=hx-bot_lastx[i],dy=hy-bot_lasty[i];if(dx*dx+dy*dy<900){bot_stuck[i]++;s->tgt=s->ang+2.0f*bot_side[i];bot_timer[i]=0;bot_tx[i]=WORLD/2;bot_ty[i]=WORLD/2;}else bot_stuck[i]=0;bot_lastx[i]=hx;bot_lasty[i]=hy;}
bot_timer[i]--;float dtx=bot_tx[i]-hx,dty=bot_ty[i]-hy;
if(bot_timer[i]<=0||dtx*dtx+dty*dty<40*40){
float best=1e9f;int bi=-1;for(int k=0;k<NPEL;k++){if(!pel[k].active)continue;float dx=pel[k].x-hx,dy=pel[k].y-hy;float d=dx*dx+dy*dy;if(d<best){best=d;bi=k;}}
if(bi>=0){bot_tx[i]=pel[bi].x;bot_ty[i]=pel[bi].y;}else{bot_tx[i]=400+frand()*(WORLD-800);bot_ty[i]=400+frand()*(WORLD-800);}
bot_timer[i]=180+rand()%120;}
float target=atan2f(bot_ty[i]-hy,bot_tx[i]-hx);
if(hx<300||hx>WORLD-300||hy<300||hy>WORLD-300){target=atan2f(WORLD/2-hy,WORLD/2-hx);bot_timer[i]=0;}
float px=hx+cosf(s->ang)*90,py=hy+sinf(s->ang)*90;float rs=radius_of(s);int danger=0;float side_sign=0;
for(int o=0;o<NSN;o++){if(o==i||!sn[o].alive)continue;float lim=(rs+radius_of(&sn[o])+20);lim*=lim;for(int j=0;j<sn[o].len;j+=3){float dx=sn[o].x[j]-px,dy=sn[o].y[j]-py;if(dx*dx+dy*dy<lim){danger=1;float cross=cosf(s->ang)*dy-sinf(s->ang)*dx;side_sign=(cross>0)?-1:1;break;}if(danger)break;}if(danger)break;}
if(danger){target=s->ang+0.9f*side_sign;bot_side[i]=(int)side_sign;}
s->tgt=target;}
static void update_snake(int i){
Snake *s=&sn[i];if(!s->alive){if(i>0&&--s->respawn<=0)spawn_snake(i);return;}s->tick++;
if(i==0){if(pad_mag>0.3f)s->tgt=atan2f(pad_ay,pad_ax);s->boost=pad_boost;}else{bot_think(i);s->boost=0;}
float d=s->tgt-s->ang;while(d>PI)d-=2*PI;while(d<-PI)d+=2*PI;if(d>0.09f)d=0.09f;if(d<-0.09f)d=-0.09f;s->ang+=d;
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
for(u32 i=0;i<W*H;i++)back[i]=0x0014222E;
int sx0=(int)((0-camx)*sc+hw),sx1=(int)((WORLD-camx)*sc+hw);int sy0=(int)((0-camy)*sc+hh),sy1=(int)((WORLD-camy)*sc+hh);u32 red=0x00380E12;
if(sx0>0)fill_rect(0,0,sx0,H,red);if(sx1<(int)W)fill_rect(sx1,0,W-sx1,H,red);if(sy0>0)fill_rect(0,0,W,sy0,red);if(sy1<(int)H)fill_rect(0,sy1,W,H-sy1,red);
for(int k=0;k<NPEL;k++){if(!pel[k].active)continue;int px=(int)((pel[k].x-camx)*sc+hw),py=(int)((pel[k].y-camy)*sc+hh);int pr=(int)((6+pel[k].size*3)*sc);if(pr<3)pr=3;u32 dark=(pel[k].col>>1)&0x007F7F7F;circle(px,py,pr+5,dark);circle(px,py,pr,pel[k].col);circle(px,py,pr/2,0x00FFFFFF);}
for(int i=NSN-1;i>=0;i--){Snake *s=&sn[i];if(!s->alive)continue;int r=(int)(radius_of(s)*sc);u32 dark=(s->col>>1)&0x007F7F7F;
for(int j=s->len-1;j>=0;j--){int px=(int)((s->x[j]-camx)*sc+hw),py=(int)((s->y[j]-camy)*sc+hh);circle(px,py,r,(j%6<3)?s->col:dark);}
int hx=(int)((s->x[0]-camx)*sc+hw),hy=(int)((s->y[0]-camy)*sc+hh);
for(int e=-1;e<=1;e+=2){int ex=hx+(int)(cosf(s->ang+e*0.65f)*r*0.65f);int ey=hy+(int)(sinf(s->ang+e*0.65f)*r*0.65f);int wr=r*45/100;if(wr<5)wr=5;int pr=r*20/100;if(pr<2)pr=2;circle(ex,ey,wr,0x00FFFFFF);circle(ex+(int)(cosf(s->ang)*wr*0.3f),ey+(int)(sinf(s->ang)*wr*0.3f),pr,0x00000000);}}
int mx=(int)W-170,my=16,ms=150;fill_rect(mx-2,my-2,ms+4,ms+4,0x00000000);fill_rect(mx,my,ms,ms,0x002A3A4A);
for(int i=0;i<NSN;i++){if(!sn[i].alive)continue;int dx=mx+(int)(sn[i].x[0]/WORLD*ms);int dy=my+(int)(sn[i].y[0]/WORLD*ms);u32 c=(i==0)?0x00FFFFFF:sn[i].col;fill_rect(dx-2,dy-2,5,5,c);}
if(!sn[0].alive&&dead_t>30){int sc2=6;if(W<1000)sc2=4;int tw=(7*4-1)*sc2;fill_rect(W/2-tw/2-12,H/2-18,tw+24,sc2*5+36,0x00000000);draw_text("APERTE X",W/2-tw/2,H/2-10,sc2,0x00FFFFFF);}}
int main(int argc,char **argv){(void)argc;(void)argv;init_screen();ioPadInit(7);sysUtilRegisterCallback(0,sysutil_cb,NULL);srand(123);init_game();while(running){sysUtilCheckCallback();read_pad();update_game();render();present();}return 0;}