// Slither.io Moderno - PS3 PSL1GHT RSX - Sem menu, inicio instantaneo
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
#define WORLD 5200.0f
#define MAXSEG 260
#define NSN 11
#define NPEL 650
#define SP 7.0f
#define PI 3.14159265f
#define TEX 64
typedef struct{float x[MAXSEG],y[MAXSEG];int len,alive,respawn,boost,grow,score,tick;float ang,tgt,blink;u32 col;u32 col2;}Snake;
typedef struct{float x,y;int size,active;float ph;u32 col;}Pellet;
static u8 texSoft[TEX*TEX];
static u8 texBall[TEX*TEX];
static u8 texRing[TEX*TEX];
static const u32 palette[11]={0x00FF4D6D,0x0038B6FF,0x0038E07A,0x00FFD23F,0x00B04DFF,0x00FF8FAB,0x0000E5C0,0x00FF9F43,0x00FF5A00,0x00FFE600,0x0000FF9E};
static gcmContextData *ctx;static u32 W,H;static u32 *fb[2];static u32 fboff[2];static u32 *back;static int cur=0,first_flip=1;static volatile int running=1;
static void sysutil_cb(u64 s,u64 p,void *u){(void)p;(void)u;if(s==SYSUTIL_EXIT_GAME)running=0;}
static void init_screen(void){
 void *host=memalign(1024*1024,HOST_SIZE);rsxInit(&ctx,CB_SIZE,HOST_SIZE,host);
 videoState st;videoGetState(VIDEO_PRIMARY,0,&st);u32 resid=st.displayMode.resolution;
 if(videoGetResolutionAvailability(VIDEO_PRIMARY,VIDEO_RESOLUTION_720,VIDEO_ASPECT_AUTO,0))resid=VIDEO_RESOLUTION_720;
 videoResolution res;videoGetResolution(resid,&res);W=res.width;H=res.height;
 videoConfiguration vc;memset(&vc,0,sizeof(vc));vc.resolution=resid;vc.format=VIDEO_BUFFER_FORMAT_XRGB;vc.aspect=VIDEO_ASPECT_AUTO;vc.pitch=W*4;
 videoConfigure(VIDEO_PRIMARY,&vc,NULL,0);videoGetState(VIDEO_PRIMARY,0,&st);gcmSetFlipMode(GCM_FLIP_VSYNC);
 for(int i=0;i<2;i++){fb[i]=(u32*)rsxMemalign(64,W*H*4);gcmAddressToOffset(fb[i],&fboff[i]);gcmSetDisplayBuffer(i,fboff[i],W*4,W,H);}
 back=(u32*)memalign(64,W*H*4);memset(back,0,W*H*4);gcmResetFlipStatus();
}
static void wait_flip(void){while(gcmGetFlipStatus()!=0)usleep(200);gcmResetFlipStatus();}
static void present(void){if(!first_flip)wait_flip();else gcmResetFlipStatus();memcpy(fb[cur],back,W*H*4);gcmSetFlip(ctx,cur);rsxFlushBuffer(ctx);gcmSetWaitFlip(ctx);cur^=1;first_flip=0;}
static void init_textures(void){
 for(int y=0;y<TEX;y++)for(int x=0;x<TEX;x++){
  float dx=(x-31.5f)/31.5f,dy=(y-31.5f)/31.5f;float d=sqrtf(dx*dx+dy*dy);if(d>1)d=1;
  float a=1.0f-d;a=a*a*(3-2*a);
  texSoft[y*TEX+x]=(u8)(a*255);
  float lx=dx*0.6f+dy*0.4f;float light=0.55f+0.45f*(1.0f-lx);if(light>1)light=1;if(light<0.25f)light=0.25f;
  float edge=1.0f-d*d;texBall[y*TEX+x]=(u8)(a*255*light*edge);
  float ring=0;if(d>0.72f&&d<1.0f)ring=(1.0f-fabsf(d-0.86f)/0.14f);if(ring<0)ring=0;
  texRing[y*TEX+x]=(u8)(ring*255);
 }
}
static inline void blend_pixel(int x,int y,u32 col,u8 alpha){
 if(x<0||x>=(int)W||y<0||y>=(int)H||alpha==0)return;
 u32 dst=back[y*W+x];int sr=(col>>16)&255,sg=(col>>8)&255,sb=col&255;
 int dr=(dst>>16)&255,dg=(dst>>8)&255,db=dst&255;int a=alpha,ia=255-a;
 back[y*W+x]=0xFF000000|(((sr*a+dr*ia)/255)<<16)|(((sg*a+dg*ia)/255)<<8)|((sb*a+db*ia)/255);
}
static void draw_soft(int cx,int cy,int r,u32 col,int alphaMax,const u8 *tex){
 if(r<1)return;if(cx+r<0||cx-r>=(int)W||cy+r<0||cy-r>=(int)H)return;
 for(int yy=-r;yy<=r;yy++){int y=cy+yy;if(y<0||y>=(int)H)continue;
  for(int xx=-r;xx<=r;xx++){int x=cx+xx;if(x<0||x>=(int)W)continue;
   int tx=(int)((xx+r)*63.0f/(r*2));int ty=(int)((yy+r)*63.0f/(r*2));if(tx<0)tx=0;if(tx>63)tx=63;if(ty<0)ty=0;if(ty>63)ty=63;
   u8 a=tex[ty*TEX+tx];if(a)blend_pixel(x,y,col,(u8)((a*alphaMax)/255));}}
}
static void fill_rect(int x,int y,int w,int h,u32 c){int x0=x<0?0:x,y0=y<0?0:y,x1=x+w,y1=y+h;if(x1>(int)W)x1=W;if(y1>(int)H)y1=H;for(int yy=y0;yy<y1;yy++){u32 *p=back+yy*W;for(int xx=x0;xx<x1;xx++)p[xx]=c;}}
static void draw_line_aa(int x0,int y0,int x1,int y1,u32 col,int alpha){int dx=abs(x1-x0),dy=abs(y1-y0);int sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=dx-dy;while(1){blend_pixel(x0,y0,col,alpha);blend_pixel(x0+1,y0,col,alpha/2);if(x0==x1&&y0==y1)break;int e2=2*err;if(e2>-dy){err-=dy;x0+=sx;}if(e2<dx){err+=dx;y0+=sy;}}}
static void draw_hex(int cx,int cy,int r,u32 col){for(int e=0;e<6;e++){double a1=(e*60-30)*PI/180.0,a2=((e+1)*60-30)*PI/180.0;int x1=cx+(int)(cos(a1)*r),y1=cy+(int)(sin(a1)*r);int x2=cx+(int)(cos(a2)*r),y2=cy+(int)(sin(a2)*r);draw_line_aa(x1,y1,x2,y2,col,90);}}
typedef struct{char c;const char *p;}Glyph;
static const Glyph glyphs[]={{'0',"###""#.#""#.#""#.#""###"},{'1',".#.""##.""#.""#.""###"},{'2',"###""..#""###""#..""###"},{'3',"###""..#""###""..#""###"},{'4',"#.#""#.#""###""..#""..#"},{'5',"###""#..""###""..#""###"},{'6',"###""#..""###""#.#""###"},{'7',"###""..#""..#""..#""..#"},{'8',"###""#.#""###""#.#""###"},{'9',"###""#.#""###""..#""###"},{'A',".#.""#.#""###""#.#""#.#"},{'B',"##.""#.#""##.""#.#""##."},{'C',"###""#..""#..""#..""###"},{'E',"###""#..""###""#..""###"},{'G',"###""#..""#.#""#.#""###"},{'M',"#.#""###""###""#.#""#.#"},{'O',"###""#.#""#.#""#.#""###"},{'P',"###""#.#""###""#..""#.."},{'R',"##.""#.#""##.""#.#""#.#"},{'S',"###""#..""###""..#""###"},{'T',"###"".#."".#."".#."".#."},{'U',"#.#""#.#""#.#""#.#""###"},{'V',"#.#""#.#""#.#""#.#"".#."},{'X',"#.#""#.#"".#.""#.#""#.#"}};
static const char *find_glyph(char c){for(unsigned i=0;i<sizeof(glyphs)/sizeof(glyphs[0]);i++)if(glyphs[i].c==c)return glyphs[i].p;return NULL;}
static void draw_text(const char *s,int x,int y,int sc,u32 c){for(;*s;s++){const char *g=find_glyph(*s);if(g)for(int r=0;r<5;r++)for(int k=0;k<3;k++)if(g[r*3+k]=='#')fill_rect(x+k*sc,y+r*sc,sc,sc,c);x+=4*sc;}}
static Snake sn[NSN];static Pellet pel[NPEL];static int pelrot=0,dead_t=0,x_prev=0,frame=0;static float camx=2600,camy=2600,zoom=1.0f;static float pad_ax=0,pad_ay=0,pad_mag=0;static int pad_boost=0,pad_x=0;
static float frand(void){return (float)rand()/(float)RAND_MAX;}
static float radius_of(const Snake *s){int l=s->len>240?240:s->len;return 10.0f+l*0.045f;}
static void add_pellet(float x,float y,int size){for(int k=0;k<NPEL;k++){int i=(pelrot+k)%NPEL;if(!pel[i].active){pel[i].x=x;pel[i].y=y;pel[i].size=size;pel[i].active=1;pel[i].col=palette[rand()%11];pel[i].ph=frand()*6.28f;pelrot=(i+1)%NPEL;return;}}}
static void spawn_snake(int i){Snake *s=&sn[i];float x=500+frand()*(WORLD-1000),y=500+frand()*(WORLD-1000);s->ang=frand()*2*PI;s->tgt=s->ang;s->len=16;s->blink=0;for(int j=0;j<s->len;j++){s->x[j]=x-cosf(s->ang)*SP*j;s->y[j]=y-sinf(s->ang)*SP*j;}s->alive=1;s->respawn=0;s->boost=0;s->grow=0;s->tick=0;s->col=palette[i%11];s->col2=palette[(i+4)%11];}
static void kill_snake(int i){Snake *s=&sn[i];for(int j=0;j<s->len;j+=2)add_pellet(s->x[j]+(frand()-0.5f)*14,s->y[j]+(frand()-0.5f)*14,2);s->alive=0;s->respawn=60;if(i==0)dead_t=0;}
static void bot_think(int i){Snake *s=&sn[i];float hx=s->x[0],hy=s->y[0];s->tgt+=(frand()-0.5f)*0.22f;if(s->tick%6==0){float best=420*420;int bi=-1;for(int k=0;k<NPEL;k++){if(!pel[k].active)continue;float dx=pel[k].x-hx,dy=pel[k].y-hy,d=dx*dx+dy*dy;if(d<best){best=d;bi=k;}}if(bi>=0)s->tgt=atan2f(pel[bi].y-hy,pel[bi].x-hx);}if(hx<300||hx>WORLD-300||hy<300||hy>WORLD-300)s->tgt=atan2f(WORLD/2-hy,WORLD/2-hx);float px2=hx+cosf(s->ang)*60,py2=hy+sinf(s->ang)*60;float rs=radius_of(s);for(int o=0;o<NSN;o++){if(o==i||!sn[o].alive)continue;float lim=rs+radius_of(&sn[o])+28;lim*=lim;for(int j=0;j<sn[o].len;j+=2){float dx=sn[o].x[j]-px2,dy=sn[o].y[j]-py2;if(dx*dx+dy*dy<lim){s->tgt=s->ang+1.7f;return;}}}}
static void update_snake(int i){Snake *s=&sn[i];if(!s->alive){if(--s->respawn<=0)spawn_snake(i);return;}s->tick++;if(i==0){if(pad_mag>0.25f)s->tgt=atan2f(pad_ay,pad_ax);s->boost=pad_boost;}else{bot_think(i);s->boost=(s->len>30&&(s->tick/90)%5==0)?1:0;}float d=s->tgt-s->ang;while(d>PI)d-=2*PI;while(d<-PI)d+=2*PI;float maxTurn=0.085f;if(d>maxTurn)d=maxTurn;if(d<-maxTurn)d=-maxTurn;s->ang+=d;float sp=3.2f;if(s->boost&&s->len>12){sp=6.2f;if(s->tick%10==0){s->len--;add_pellet(s->x[s->len],s->y[s->len],1);}}s->x[0]+=cosf(s->ang)*sp;s->y[0]+=sinf(s->ang)*sp;for(int j=1;j<s->len;j++){float dx=s->x[j-1]-s->x[j],dy=s->y[j-1]-s->y[j];float dd=sqrtf(dx*dx+dy*dy);if(dd>SP){float f=(dd-SP)/dd;s->x[j]+=dx*f;s->y[j]+=dy*f;}}if(s->x[0]<40||s->x[0]>WORLD-40||s->y[0]<40||s->y[0]>WORLD-40){kill_snake(i);return;}float rs=radius_of(s),er=rs+14;for(int k=0;k<NPEL;k++){if(!pel[k].active)continue;float dx=pel[k].x-s->x[0],dy=pel[k].y-s->y[0];if(dx*dx+dy*dy<er*er){pel[k].active=0;s->grow+=pel[k].size;s->score+=pel[k].size*5;}}while(s->grow>=2&&s->len<MAXSEG){s->x[s->len]=s->x[s->len-1];s->y[s->len]=s->y[s->len-1];s->len++;s->grow-=2;}if(s->len>=MAXSEG)s->grow=0;for(int o=0;o<NSN;o++){if(o==i||!sn[o].alive)continue;float lim=(rs+radius_of(&sn[o]))*0.72f;lim*=lim;for(int j=4;j<sn[o].len;j++){float dx=sn[o].x[j]-s->x[0],dy=sn[o].y[j]-s->y[0];if(dx*dx+dy*dy<lim){kill_snake(i);return;}}}}
static void init_game(void){memset(pel,0,sizeof(pel));for(int k=0;k<NPEL;k++)add_pellet(frand()*WORLD,frand()*WORLD,1+(rand()%5==0));for(int i=0;i<NSN;i++){sn[i].score=0;spawn_snake(i);}sn[0].x[0]=WORLD/2;sn[0].y[0]=WORLD/2;for(int j=0;j<sn[0].len;j++){sn[0].x[j]=WORLD/2-j*SP;sn[0].y[j]=WORLD/2;}camx=WORLD/2;camy=WORLD/2;zoom=1.0f;dead_t=0;}
static void read_pad(void){padInfo info;padData d;pad_ax=pad_ay=pad_mag=0;pad_boost=pad_x=0;ioPadGetInfo(&info);if(info.status[0]){ioPadGetData(0,&d);float ax=((int)d.ANA_L_H-128)/128.0f,ay=((int)d.ANA_L_V-128)/128.0f;if(d.BTN_LEFT)ax=-1;if(d.BTN_RIGHT)ax=1;if(d.BTN_UP)ay=-1;if(d.BTN_DOWN)ay=1;if(fabsf(ax)<0.15f)ax=0;if(fabsf(ay)<0.15f)ay=0;pad_ax=ax;pad_ay=ay;pad_mag=sqrtf(ax*ax+ay*ay);pad_boost=d.BTN_CROSS||d.BTN_R1||d.BTN_R2;pad_x=d.BTN_CROSS;}}
static void update_game(void){frame++;for(int i=0;i<NSN;i++)update_snake(i);int cnt=0;for(int k=0;k<NPEL;k++)cnt+=pel[k].active;for(int n=0;n<2&&cnt<NPEL-40;n++,cnt++)add_pellet(frand()*WORLD,frand()*WORLD,1+(rand()%5==0));if(sn[0].alive){camx+=(sn[0].x[0]-camx)*0.10f;camy+=(sn[0].y[0]-camy)*0.10f;}else{dead_t++;if(dead_t>60){spawn_snake(0);sn[0].score=0;sn[0].x[0]=camx;sn[0].y[0]=camy;}}x_prev=pad_x;float targetZoom=1.0f-(sn[0].len*0.0016f);if(targetZoom<0.62f)targetZoom=0.62f;if(targetZoom>1.0f)targetZoom=1.0f;zoom+=(targetZoom-zoom)*0.05f;}
static void render(void){
 float sc=(float)W/1280.0f*zoom;float hw=W/2.0f,hh=H/2.0f;u32 n=W*H;for(u32 i=0;i<n;i++)back[i]=0x00161C22;
 int hexR=(int)(72*sc);if(hexR<28)hexR=28;int hexW=(int)(hexR*1.732f);int hexH=hexR*3/2;
 int offX=-(int)(camx*sc)%hexW;int offY=-(int)(camy*sc)%hexH;if(offX>0)offX-=hexW;if(offY>0)offY-=hexH;
 for(int gy=-hexH;gy<(int)H+hexH;gy+=hexH)for(int gx=-hexW;gx<(int)W+hexW;gx+=hexW){int cx=gx+offX+(((gy/hexH)%2==0)?0:hexW/2);int cy=gy+offY;draw_hex(cx,cy,hexR,0x002A3644);}
 int bx0=(int)((0-camx)*sc+hw),by0=(int)((0-camy)*sc+hh),bx1=(int)((WORLD-camx)*sc+hw),by1=(int)((WORLD-camy)*sc+hh);
 if(bx0>-20||by0>-20||bx1<(int)W+20||by1<(int)H+20){for(int x=bx0;x<=bx1;x+=3){blend_pixel(x,by0,0x00FF2E2E,220);blend_pixel(x,by1,0x00FF2E2E,220);}for(int y=by0;y<=by1;y+=3){blend_pixel(bx0,y,0x00FF2E2E,220);blend_pixel(bx1,y,0x00FF2E2E,220);}}
 for(int k=0;k<NPEL;k++){if(!pel[k].active)continue;int px=(int)((pel[k].x-camx)*sc+hw),py=(int)((pel[k].y-camy)*sc+hh);if(px<-40||px>(int)W+40||py<-40||py>(int)H+40)continue;float pulse=1.0f+0.28f*sinf(frame*0.08f+pel[k].ph);int r=(int)((5+pel[k].size*2.2f)*sc*pulse);if(r<3)r=3;draw_soft(px,py,r*3,pel[k].col,70,texSoft);draw_soft(px,py,r,pel[k].col,255,texBall);draw_soft(px,py,r/2,0x00FFFFFF,255,texSoft);}
 for(int ord=0;ord<2;ord++)for(int i=0;i<NSN;i++){
  if(!sn[i].alive)continue;if(ord==0&&i==0)continue;if(ord==1&&i!=0)continue;
  Snake *s=&sn[i];float rs=radius_of(s)*sc;if(rs<6)rs=6;
  if(s->boost){int gx=(int)((s->x[0]-camx)*sc+hw),gy=(int)((s->y[0]-camy)*sc+hh);draw_soft(gx,gy,(int)(rs*2.6f),0x00FFE600,90,texSoft);}
  for(int j=s->len-1;j>=0;j--){int px=(int)((s->x[j]-camx)*sc+hw),py=(int)((s->y[j]-camy)*sc+hh);if(px<-60||px>(int)W+60||py<-60||py>(int)H+60)continue;u32 col=(j/3)%2==0?s->col:s->col2;draw_soft(px,py,(int)(rs*1.35f),col,60,texSoft);draw_soft(px,py,(int)rs,col,255,texBall);draw_soft(px-(int)(rs*0.3f),py-(int)(rs*0.35f),(int)(rs*0.35f),0x00FFFFFF,110,texSoft);}
  int hx=(int)((s->x[0]-camx)*sc+hw),hy=(int)((s->y[0]-camy)*sc+hh);draw_soft(hx,hy,(int)(rs*1.4f),s->col,255,texBall);
  float perp=s->ang+PI/2;int eyeOff=(int)(rs*0.55f),fwd=(int)(rs*0.55f);
  int ex1=hx+(int)(cosf(s->ang)*fwd+cosf(perp)*eyeOff),ey1=hy+(int)(sinf(s->ang)*fwd+sinf(perp)*eyeOff);
  int ex2=hx+(int)(cosf(s->ang)*fwd-cosf(perp)*eyeOff),ey2=hy+(int)(sinf(s->ang)*fwd-sinf(perp)*eyeOff);
  int er=(int)(rs*0.42f)+2;if(er<4)er=4;draw_soft(ex1,ey1,er,0x00FFFFFF,255,texSoft);draw_soft(ex2,ey2,er,0x00FFFFFF,255,texSoft);
  int pr=er/2+1;draw_soft(ex1+(int)(cosf(s->ang)*2),ey1+(int)(sinf(s->ang)*2),pr,0x00000000,255,texSoft);draw_soft(ex2+(int)(cosf(s->ang)*2),ey2+(int)(sinf(s->ang)*2),pr,0x00000000,255,texSoft);
  draw_soft(ex1+2,ey1-2,2,0x00FFFFFF,255,texSoft);draw_soft(ex2+2,ey2-2,2,0x00FFFFFF,255,texSoft);
 }
 char buf[32];int ui=(int)(4*sc+0.5f);if(ui<3)ui=3;
 draw_soft(90,42,60,0x00000000,120,texSoft);draw_text("SCORE",24,20,ui,0x00FFFFFF);sprintf(buf,"%d",sn[0].score);draw_text(buf,24,20+7*ui,ui+2,0x00FFD23F);
 draw_text("X TURBO",24,(int)H-8*ui,ui,0x00809BBF);
 int mw=170,mh=110,mx=(int)W-mw-18,my=(int)H-mh-18;fill_rect(mx,my,mw,mh,0x00000000);fill_rect(mx+2,my+2,mw-4,mh-4,0x00161C22);
 for(int i=0;i<NSN;i++){if(!sn[i].alive)continue;int px=mx+(int)(sn[i].x[0]/WORLD*mw),py=my+(int)(sn[i].y[0]/WORLD*mh);draw_soft(px,py,i==0?5:4,i==0?0x00FFE600:sn[i].col,255,texSoft);}
 if(!sn[0].alive)draw_text("GAME OVER",(int)W/2-70,(int)H/2-20,ui*2,0x00FF4D4D);
}
int main(int argc,char **argv){(void)argc;(void)argv;init_screen();init_textures();ioPadInit(7);sysUtilRegisterCallback(0,sysutil_cb,NULL);srand(20261109);init_game();while(running){sysUtilCheckCallback();read_pad();update_game();render();present();}return 0;}