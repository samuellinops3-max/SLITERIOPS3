// Cobra Arena - FIX CONTROLE - PSL1GHT + Tiny3D - 100% completo
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <ppu-types.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <io/pad.h>
#include <tiny3d.h>

#define SCR_W 1280.0f
#define SCR_H 720.0f
#define WORLD 3200.0f
#define MAXSEG 420
#define NSN 12
#define NPEL 520
#define SP 7.0f
#define PI 3.14159265f
#define BG 0xFF161C22

typedef struct{float x[MAXSEG],y[MAXSEG];int len,alive,respawn,boost,grow,score,tick;float ang,tgt;u32 col;} Snake;
typedef struct{float x,y;int size,active;u32 col;} Pellet;

static const u32 palette[8]={0xFFFF4D4D,0xFF55D6FF,0xFF66FF66,0xFFFFD23F,0xFFC77DFF,0xFFFF8FAB,0xFF00E5C0,0xFFFF9F43};
static volatile int running=1;
static float zoom=1.0f;
static void sys_cb(u64 s,u64 p,void*u){(void)p;(void)u;if(s==SYSUTIL_EXIT_GAME)running=0;}

static Snake sn[NSN];
static Pellet pel[NPEL];
static int pelrot=0,dead_t=0,x_prev=0,pad_port=-1;
static float camx=WORLD/2,camy=WORLD/2;
static float pad_ax=0,pad_ay=0,pad_mag=0;
static int pad_boost=0,pad_x=0,pad_connected=0;
static u32 *texCircle=NULL,*texGlow=NULL,*texHex=NULL;

static float frand(void){return (float)rand()/(float)RAND_MAX;}
static u32 rgba(u8 r,u8 g,u8 b,u8 a){return ((u32)a<<24)|((u32)r<<16)|((u32)g<<8)|b;}

static void buildTex(void){
  texCircle=tiny3d_AllocTexture(TINY3D_TEX_FORMAT_A8R8G8B8,128,128);
  texGlow=tiny3d_AllocTexture(TINY3D_TEX_FORMAT_A8R8G8B8,128,128);
  texHex=tiny3d_AllocTexture(TINY3D_TEX_FORMAT_A8R8G8B8,256,256);
  for(int y=0;y<128;y++)for(int x=0;x<128;x++){
    float dx=(x-63.5f)/63.5f,dy=(y-63.5f)/63.5f;float d=sqrtf(dx*dx+dy*dy);
    float a=0;if(d<1.0f){float e=(1.0f-d)/0.07f;if(e>1)e=1;a=e*255;}
    texCircle[y*128+x]=rgba(255,255,255,(u8)a);
    float ag=0;if(d<1.0f)ag=expf(-d*d*4.2f)*255.0f;
    texGlow[y*128+x]=rgba(255,255,255,(u8)ag);
  }
  for(int y=0;y<256;y++)for(int x=0;x<256;x++){
    texHex[y*256+x]=0xFF161C22;
    float gx=fmodf((float)x,64.0f);if(gx<0)gx+=64;float gy=fmodf((float)y,64.0f);if(gy<0)gy+=64;
    float dd=fminf(fminf(gx,64-gx),fminf(gy,64-gy));
    if(dd<1.0f){float a=1.0f-dd;u8 v=(u8)(0x28+a*16);texHex[y*256+x]=rgba(v,v+5,v+12,255);}
  }
}
static void drawTex(float cx,float cy,float s,u32 col,u32 *tex){
  tiny3d_SetTexture(0,tex,128,128,128*4,TINY3D_TEX_FORMAT_A8R8G8B8,TINY3D_TEXTURE_LINEAR,0);
  tiny3d_DrawQuad(cx-s/2,cy-s/2,cx+s/2,cy+s/2,0,0,1,1,col);
}
static void drawGlow(float cx,float cy,float s,u32 col){
  tiny3d_SetBlend(1,TINY3D_BLEND_FUNC_SRC_ALPHA,TINY3D_BLEND_FUNC_ONE,TINY3D_BLEND_FUNC_SRC_ALPHA,TINY3D_BLEND_FUNC_ONE);
  drawTex(cx,cy,s,col,texGlow);
  tiny3d_SetBlend(0,TINY3D_BLEND_FUNC_SRC_ALPHA,TINY3D_BLEND_FUNC_ONE_MINUS_SRC_ALPHA,TINY3D_BLEND_FUNC_SRC_ALPHA,TINY3D_BLEND_FUNC_ONE_MINUS_SRC_ALPHA);
}
static void glow(float cx,float cy,float r,u32 rgb,u32 a){u32 c=(rgb&0x00FFFFFF)|(a<<24);drawTex(cx,cy,r*2,c,texCircle);}
static float SX(float x){return (x-camx)*zoom+SCR_W*0.5f;}
static float SY(float y){return (y-camy)*zoom+SCR_H*0.5f;}

typedef struct{char c;const char *p;} Glyph;
static const Glyph glyphs[]={
  {'0',"###" "#.#" "#.#" "#.#" "###"},{'1',".#." "##." ".#." ".#." "###"},
  {'2',"###" "..#" "###" "#.." "###"},{'3',"###" "..#" "###" "..#" "###"},
  {'4',"#.#" "#.#" "###" "..#" "..#"},{'5',"###" "#.." "###" "..#" "###"},
  {'6',"###" "#.." "###" "#.#" "###"},{'7',"###" "..#" "..#" "..#" "..#"},
  {'8',"###" "#.#" "###" "#.#" "###"},{'9',"###" "#.#" "###" "..#" "###"},
  {'A',".#." "#.#" "###" "#.#" "#.#"},{'B',"##." "#.#" "##." "#.#" "##."},
  {'C',"###" "#.." "#.." "#.." "###"},{'D',"##." "#.#" "#.#" "#.#" "##."},
  {'E',"###" "#.." "###" "#.." "###"},{'G',"###" "#.." "#.#" "#.#" "###"},
  {'M',"#.#" "###" "###" "#.#" "#.#"},{'O',"###" "#.#" "#.#" "#.#" "###"},
  {'P',"###" "#.#" "###" "#.." "#.."},{'R',"##." "#.#" "##." "#.#" "#.#"},
  {'S',"###" "#.." "###" "..#" "###"},{'T',"###" ".#." ".#." ".#." ".#."},
  {'U',"#.#" "#.#" "#.#" "#.#" "###"},{'V',"#.#" "#.#" "#.#" "#.#" ".#."},
  {'X',"#.#" "#.#" ".#." "#.#" "#.#"},{'N',"#.#" "###" "###" "#.#" "#.#"},
  {'L',"#.." "#.." "#.." "#.." "###"},{'H',"#.#" "#.#" "###" "#.#" "#.#"},
  {'I',"###" ".#." ".#." ".#." "###"},{'F',"###" "#.." "###" "#.." "#.."},
  {'-', "..." "..." "###" "..." "..."},{'.', "..." "..." "..." "..." "##."},
};
static const char *find_glyph(char c){for(unsigned i=0;i<sizeof(glyphs)/sizeof(glyphs[0]);i++)if(glyphs[i].c==c)return glyphs[i].p;return NULL;}
static float text_width(const char *s,float sc){return (float)strlen(s)*4.0f*sc*1.3f;}
static void draw_text(const char *s,float x,float y,float sc,u32 rgb){
  for(;*s;s++){const char *g=find_glyph(*s);
    if(g){for(int r=0;r<5;r++)for(int k=0;k<3;k++)if(g[r*3+k]=='#')glow(x+k*sc*1.3f+sc*0.5f,y+r*sc*1.3f+sc*0.5f,sc*0.55f,rgb,255);}
    x+=4.0f*sc*1.3f;}
}

static float radius_of(const Snake *s){int l=s->len>240?240:s->len;return 9.0f+l*0.035f;}
static void add_pellet(float x,float y,int size){
  for(int k=0;k<NPEL;k++){int i=(pelrot+k)%NPEL;if(!pel[i].active){pel[i].x=x;pel[i].y=y;pel[i].size=size;pel[i].active=1;pel[i].col=palette[rand()%8];pelrot=(i+1)%NPEL;return;}}
}
static void spawn_snake(int i){
  Snake *s=&sn[i];float x=400+frand()*(WORLD-800),y=400+frand()*(WORLD-800);
  s->ang=frand()*2*PI;s->tgt=s->ang;s->len=10;
  for(int j=0;j<s->len;j++){s->x[j]=x-cosf(s->ang)*SP*j;s->y[j]=y-sinf(s->ang)*SP*j;}
  s->alive=1;s->respawn=0;s->boost=0;s->grow=0;s->tick=0;s->col=palette[i%8];
}
static void kill_snake(int i){
  Snake *s=&sn[i];
  for(int j=0;j<s->len;j+=2)add_pellet(s->x[j]+(frand()-0.5f)*10,s->y[j]+(frand()-0.5f)*10,2);
  s->alive=0;s->respawn=240;if(i==0)dead_t=0;
}
static void bot_think(int i){
  Snake *s=&sn[i];float hx=s->x[0],hy=s->y[0];
  if(s->tick%6==0){
    float best=380*380;int bi=-1;
    for(int k=0;k<NPEL;k++){if(!pel[k].active)continue;float dx=pel[k].x-hx,dy=pel[k].y-hy,d=dx*dx+dy*dy;if(d<best){best=d;bi=k;}}
    if(bi>=0)s->tgt=atan2f(pel[bi].y-hy,pel[bi].x-hx);
  }
  if(hx<250||hx>WORLD-250||hy<250||hy>WORLD-250)s->tgt=atan2f(WORLD/2-hy,WORLD/2-hx);
  float px=hx+cosf(s->ang)*55,py=hy+sinf(s->ang)*55;float rs=radius_of(s);
  for(int o=0;o<NSN;o++){if(o==i||!sn[o].alive)continue;float lim=rs+radius_of(&sn[o])+25;lim*=lim;
    for(int j=0;j<sn[o].len;j+=2){float dx=sn[o].x[j]-px,dy=sn[o].y[j]-py;if(dx*dx+dy*dy<lim){s->tgt=s->ang+1.6f;return;}}}
}

// ===== CONTROLE ARRUMADO - ESSA ERA A PARTE BUGADA =====
static void read_pad(void){
  pad_ax=0;pad_ay=0;pad_mag=0;pad_boost=0;pad_x=0;pad_connected=0;
  padInfo info;memset(&info,0,sizeof(info));
  ioPadGetInfo(&info);
  // Acha qualquer controle conectado, nao so a porta 0
  int port=-1;
  for(int p=0;p<4;p++){if(info.status[p]){port=p;break;}}
  if(port<0){pad_port=-1;return;}
  pad_port=port;pad_connected=1;
  padData d;memset(&d,0,sizeof(d));
  if(ioPadGetData(port,&d)!=0)return;
  // Analogico - 128 = centro
  float ax=((float)d.ANA_L_H-128.0f)/127.0f;
  float ay=((float)d.ANA_L_V-128.0f)/127.0f;
  // Setinha tem prioridade se apertar - por isso nao mexia antes, tava invertido
  if(d.BTN_LEFT)ax=-1.0f;
  if(d.BTN_RIGHT)ax=1.0f;
  if(d.BTN_UP)ay=-1.0f;
  if(d.BTN_DOWN)ay=1.0f;
  if(ax>1)ax=1;if(ax<-1)ax=-1;if(ay>1)ay=1;if(ay<-1)ay=-1;
  float mag=sqrtf(ax*ax+ay*ay);
  // Deadzone 0.38 - se nao tiver, a cobra anda sozinha
  if(mag<0.38f){ax=0;ay=0;mag=0;}
  else if(mag>1.0f){ax/=mag;ay/=mag;mag=1.0f;}
  pad_ax=ax;pad_ay=ay;pad_mag=mag;
  // Turbo: X ou R2 com pressao >80, R1 nao liga sozinho mais
  int xHeld=d.BTN_CROSS?1:0;
  int r2Held=(d.PRE_R2>80||d.BTN_R2)?1:0;
  pad_boost=(xHeld||r2Held)?1:0;
  pad_x=xHeld;
}

static void update_snake(int i){
  Snake *s=&sn[i];
  if(!s->alive){if(i>0&&--s->respawn<=0)spawn_snake(i);return;}
  s->tick++;
  if(i==0){
    // Se mexeu analogico ou seta, vira pra la. Se nao mexeu, continua reto.
    if(pad_mag>0.38f)s->tgt=atan2f(pad_ay,pad_ax);
    s->boost=pad_boost;
  }else{bot_think(i);s->boost=(s->len>30&&(s->tick/90)%5==0)?1:0;}
  float d=s->tgt-s->ang;while(d>PI)d-=2*PI;while(d<-PI)d+=2*PI;
  if(d>0.075f)d=0.075f;if(d<-0.075f)d=-0.075f;s->ang+=d;
  float sp=3.0f;
  if(s->boost&&s->len>12){sp=5.4f;if(s->tick%10==0){s->len--;add_pellet(s->x[s->len],s->y[s->len],1);}}
  s->x[0]+=cosf(s->ang)*sp;s->y[0]+=sinf(s->ang)*sp;
  for(int j=1;j<s->len;j++){
    float dx=s->x[j-1]-s->x[j],dy=s->y[j-1]-s->y[j];float dd=sqrtf(dx*dx+dy*dy);
    if(dd>SP){float f=(dd-SP)/dd;s->x[j]+=dx*f;s->y[j]+=dy*f;}
  }
  if(s->x[0]<0||s->x[0]>WORLD||s->y[0]<0||s->y[0]>WORLD){kill_snake(i);return;}
  float rs=radius_of(s),er=rs+12;
  for(int k=0;k<NPEL;k++){if(!pel[k].active)continue;float dx=pel[k].x-s->x[0],dy=pel[k].y-s->y[0];if(dx*dx+dy*dy<er*er){pel[k].active=0;s->grow+=pel[k].size;s->score+=pel[k].size*5;}}
  while(s->grow>=2&&s->len<MAXSEG){s->x[s->len]=s->x[s->len-1];s->y[s->len]=s->y[s->len-1];s->len++;s->grow-=2;}
  if(s->len>=MAXSEG)s->grow=0;
  for(int o=0;o<NSN;o++){if(o==i||!sn[o].alive)continue;float lim=(rs+radius_of(&sn[o]))*0.75f;lim*=lim;
    for(int j=0;j<sn[o].len;j++){float dx=sn[o].x[j]-s->x[0],dy=sn[o].y[j]-s->y[0];if(dx*dx+dy*dy<lim){kill_snake(i);return;}}}
}
static void init_game(void){
  memset(pel,0,sizeof(pel));
  for(int k=0;k<NPEL;k++)add_pellet(frand()*WORLD,frand()*WORLD,1+(rand()%5==0));
  for(int i=0;i<NSN;i++){sn[i].score=0;spawn_snake(i);}
  dead_t=0;
}
static void update_game(void){
  for(int i=0;i<NSN;i++)update_snake(i);
  int count=0;for(int k=0;k<NPEL;k++)count+=pel[k].active;
  for(int n=0;n<2&&count<NPEL-40;n++,count++)add_pellet(frand()*WORLD,frand()*WORLD,1+(rand()%5==0));
  if(sn[0].alive){
    camx+=(sn[0].x[0]-camx)*0.12f;camy+=(sn[0].y[0]-camy)*0.12f;
    float tz=1.0f/(0.9f+(float)sn[0].len/600.0f);if(tz<0.5f)tz=0.5f;if(tz>1.0f)tz=1.0f;
    zoom+=(tz-zoom)*0.04f;
  }else{
    dead_t++;
    if(dead_t>60&&pad_x&&!x_prev){spawn_snake(0);sn[0].score=0;camx=sn[0].x[0];camy=sn[0].y[0];}
  }
  x_prev=pad_x;
}
static void draw_grid(void){
  float vw=SCR_W/zoom,vh=SCR_H/zoom;
  float u0=(camx-vw/2)/256.0f,v0=(camy-vh/2)/256.0f,u1=(camx+vw/2)/256.0f,v1=(camy+vh/2)/256.0f;
  tiny3d_SetTexture(0,texHex,256,256,256*4,TINY3D_TEX_FORMAT_A8R8G8B8,TINY3D_TEXTURE_LINEAR,1);
  tiny3d_DrawQuad(0,0,SCR_W,SCR_H,u0,v0,u1,v1,0xFFFFFFFF);
}
static void draw_arena_edge(void){
  float r=6.0f*zoom+1.0f;
  for(float t=0;t<=WORLD;t+=40.0f){
    glow(SX(t),SY(0),r,0xFF3355,230);glow(SX(t),SY(WORLD),r,0xFF3355,230);
    glow(SX(0),SY(t),r,0xFF3355,230);glow(SX(WORLD),SY(t),r,0xFF3355,230);
  }
}
static void draw_pellets(void){
  for(int k=0;k<NPEL;k++){
    if(!pel[k].active)continue;float x=SX(pel[k].x),y=SY(pel[k].y);
    if(x<-40||x>SCR_W+40||y<-40||y>SCR_H+40)continue;
    float r=(3.0f+pel[k].size*1.5f)*zoom;
    drawGlow(x,y,r*2.4f,pel[k].col);glow(x,y,r,pel[k].col,255);glow(x,y,r*0.4f,0xFFFFFF,255);
  }
}
static void draw_snake(const Snake *s,int full){
  float r=radius_of(s)*zoom;int step=full?1:2;u32 col=s->col;
  for(int j=s->len-1;j>=0;j-=step){
    float x=SX(s->x[j]),y=SY(s->y[j]);if(x<-r*2||x>SCR_W+r*2||y<-r*2||y>SCR_H+r*2)continue;
    if(full)glow(x+3*zoom,y+5*zoom,r*1.05f,0x000000,90);
    glow(x,y,r,col,255);
    if((j%4)==0)glow(x-r*0.25f,y-r*0.3f,r*0.42f,0xFFFFFF,110);
  }
  float hx=SX(s->x[0]),hy=SY(s->y[0]);float hr=r*1.15f;
  glow(hx+3*zoom,hy+5*zoom,hr*1.05f,0x000000,90);glow(hx,hy,hr,col,255);glow(hx-hr*0.2f,hy-hr*0.3f,hr*0.4f,0xFFFFFF,140);
  float fx=cosf(s->ang),fy=sinf(s->ang);float px=-fy,py=fx;
  float ex=hx+fx*hr*0.4f,ey=hy+fy*hr*0.4f;
  glow(ex+px*hr*0.45f,ey+py*hr*0.45f,hr*0.22f,0xFFFFFF,255);glow(ex-px*hr*0.45f,ey-py*hr*0.45f,hr*0.22f,0xFFFFFF,255);
  glow(ex+px*hr*0.45f+fx*hr*0.08f,ey+py*hr*0.45f+fy*hr*0.08f,hr*0.10f,0x000000,255);
  glow(ex-px*hr*0.45f+fx*hr*0.08f,ey-py*hr*0.45f+fy*hr*0.08f,hr*0.10f,0x000000,255);
}
static void draw_minimap(void){
  float mx=SCR_W-90.0f,my=90.0f,mr=62.0f;glow(mx,my,mr*1.05f,0x000000,140);
  for(int k=NSN-1;k>=0;k--){if(!sn[k].alive)continue;float x=mx+sn[k].x[0]/WORLD*mr*2.0f-mr;float y=my+sn[k].y[0]/WORLD*mr*2.0f-mr;glow(x,y,k==0?5.0f:3.0f,sn[k].col,255);}
}
static void draw_hud(void){
  char buf[32];float sc=3.0f;
  draw_text("SCORE",24,20,sc,0xFFFFFF);
  sprintf(buf,"%d",sn[0].score);draw_text(buf,24,20+7*sc*1.3f,sc+1.0f,0xFFD23F);
  draw_text("X TURBO",24,SCR_H-40,sc,0x809BBF);
  // DEBUG DO CONTROLE - pra tu ver se ele ta lendo
  if(!pad_connected)draw_text("CONTROLE NAO DETECTADO",24,SCR_H-70,sc,0xFF4D4D);
  else{draw_text("CONTROLE OK",24,SCR_H-70,sc,0x66FF66);}
  int order[NSN];for(int i=0;i<NSN;i++)order[i]=i;
  for(int i=0;i<NSN;i++)for(int j=i+1;j<NSN;j++)if(sn[order[j]].score>sn[order[i]].score){int t=order[i];order[i]=order[j];order[j]=t;}
  for(int r=0;r<5;r++){int k=order[r];float y=190.0f+r*26.0f;glow(SCR_W-170.0f,y+6.0f,6.0f,sn[k].col,255);sprintf(buf,"%d",sn[k].score);draw_text(buf,SCR_W-150.0f,y,2.5f,0xFFFFFF);}
  if(!sn[0].alive){
    glow(SCR_W*0.5f,SCR_H*0.5f,420.0f,0x000000,130);
    const char *t1="GAME OVER";draw_text(t1,SCR_W*0.5f-text_width(t1,6.0f)*0.5f,SCR_H*0.5f-60,6.0f,0xFF4D4D);
    if(dead_t>60){const char *t2="PRESS X";draw_text(t2,SCR_W*0.5f-text_width(t2,3.5f)*0.5f,SCR_H*0.5f+20,3.5f,0xFFFFFF);}
  }
}
static void render(void){
  tiny3d_Clear(BG,TINY3D_CLEAR_ALL);tiny3d_Project2D();
  draw_grid();draw_arena_edge();draw_pellets();
  for(int i=NSN-1;i>=1;i--)if(sn[i].alive)draw_snake(&sn[i],0);
  if(sn[0].alive)draw_snake(&sn[0],1);
  draw_minimap();draw_hud();
}
int main(void){
  sysUtilRegisterCallback(0,sys_cb,NULL);
  ioPadInit(7);
  tiny3d_Init(4*1024*1024);
  videoState st;videoGetState(VIDEO_PRIMARY,0,&st);
  u32 res=VIDEO_RESOLUTION_720;videoResolution r;videoGetResolution(res,&r);
  videoConfiguration vc;memset(&vc,0,sizeof(vc));vc.resolution=res;vc.format=VIDEO_BUFFER_FORMAT_XRGB;vc.aspect=VIDEO_ASPECT_AUTO;vc.pitch=r.width*4;
  videoConfigure(VIDEO_PRIMARY,&vc,NULL,0);
  tiny3d_Project2D();buildTex();
  srand(20261109);init_game();
  while(running){sysUtilCheckCallback();read_pad();update_game();render();tiny3d_Flip();}
  ioPadEnd();return 0;
}