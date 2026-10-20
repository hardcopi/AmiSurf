/* asb-sdl — Chrome client (ASBC). Chrome does the page; we draw chrome + blit JPEGs. */
#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include "../chrome/asbc.h"
#define QOI_IMPLEMENTATION
#define QOI_NO_STDIO
#include "../chrome/qoi.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_MAX_DIMENSIONS 8192
#include "../third_party/stb/stb_image.h"

#define ASB_VERSION "0.88"
static int g_tabh=36, g_toolh=44, g_stath=24, g_full, g_zoom=100, g_findcur, g_findtot, g_uisize=16;
#define TABH g_tabh
#define TOOLH g_toolh
#define TOP (g_full?0:(TABH+TOOLH))
#define CH_TABBG 0xDEE1E6
#define CH_TAB   0xFFFFFF
#define CH_TOOL  0xF1F3F4
#define CH_OMNI  0xFFFFFF
#define CH_LINE  0xDADCE0
#define CH_FG    0x202124
#define CH_MUTED 0x5F6368
#define CH_ACCENT 0x1A73E8
#define CH_BG    0xFFFFFF

static SDL_Window *Wn; static SDL_Renderer *R;
static int g_W=1400, g_H=800;
static int g_fd=-1, g_can_back=1, g_can_fwd=1, g_loading;
static char g_url[2400]="https://example.com/", g_title[256]="AmiSurf", g_status[400];
static char g_edit[2400], g_host[128]="127.0.0.1";
static int g_port=ASBC_PORT, g_omni_sel;
static unsigned char *g_jpeg_buf;
static uint32_t g_jpeg_cap=ASBC_MAX_MSG;
static SDL_Texture *g_page;
static int g_pw, g_ph;
static SDL_Cursor *cur_arrow, *cur_hand, *cur_ibeam, *cur_wait;

enum { HIT_NONE=0, HIT_BACK, HIT_FWD, HIT_RELOAD, HIT_OMNI, HIT_MENU };

static const char *fontdir(void){
  static const char *d; if(d) return d;
  /* ASB_FONTDIR first if set (a NULL there used to end the list, so no font was ever found) */
  const char *env=getenv("ASB_FONTDIR");
  const char *c[]={env&&env[0]?env:"/usr/share/fonts/liberation-sans-fonts/","/usr/share/fonts/liberation-sans-fonts/","/usr/share/fonts/truetype/liberation/","/usr/share/fonts/liberation/","/usr/share/fonts/TTF/",NULL};
  for(int i=0;c[i];i++){ char p[600]; snprintf(p,sizeof p,"%s/LiberationSans-Regular.ttf",c[i]); FILE*f=fopen(p,"rb"); if(f){ fclose(f); return d=c[i]; } }
  return d=""; }
static TTF_Font *open_font(int sz){
  char p[700]; TTF_Font *f;
  if(getenv("ASB_UIFONT") && (f=TTF_OpenFont(getenv("ASB_UIFONT"),sz))) return f; /* -uifont /path/to/font.ttf */
  snprintf(p,sizeof p,"%s/LiberationSans-Regular.ttf",fontdir());
  f=TTF_OpenFont(p,sz); if(f) return f;
  const char *alts[]={"/usr/share/fonts/rsms-inter-fonts/Inter-Regular.ttf","/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",NULL};
  for(int i=0;alts[i];i++){ f=TTF_OpenFont(alts[i],sz); if(f) return f; }
  return NULL; }
static void fill(int x,int y,int w,int h,unsigned rgb){
  SDL_SetRenderDrawColor(R,rgb>>16&255,rgb>>8&255,rgb&255,255);
  SDL_Rect r={x,y,w,h}; SDL_RenderFillRect(R,&r); }
static void frame(int x,int y,int w,int h,unsigned rgb){
  SDL_SetRenderDrawColor(R,rgb>>16&255,rgb>>8&255,rgb&255,255);
  SDL_Rect r={x,y,w,h}; SDL_RenderDrawRect(R,&r); }
static void fillr(int x,int y,int w,int h,unsigned rgb,int rad){
  if(rad<1){ fill(x,y,w,h,rgb); return; }
  if(rad>w/2) rad=w/2; if(rad>h/2) rad=h/2;
  fill(x+rad,y,w-2*rad,h,rgb); fill(x,y+rad,w,h-2*rad,rgb);
  SDL_SetRenderDrawColor(R,rgb>>16&255,rgb>>8&255,rgb&255,255);
  for(int i=0;i<rad;i++) for(int j=0;j<rad;j++) if(i*i+j*j<=rad*rad){
    SDL_RenderDrawPoint(R,x+rad-1-i,y+rad-1-j);
    SDL_RenderDrawPoint(R,x+w-rad+i,y+rad-1-j);
    SDL_RenderDrawPoint(R,x+rad-1-i,y+h-rad+j);
    SDL_RenderDrawPoint(R,x+w-rad+i,y+h-rad+j); }
}
static void text(TTF_Font *f,int x,int y,const char *s,unsigned rgb){
  if(!f||!s||!s[0]) return;
  SDL_Color col={rgb>>16&255,rgb>>8&255,rgb&255,255};
  SDL_Surface *sf=TTF_RenderUTF8_Blended(f,s,col); if(!sf) return;
  SDL_Texture *t=SDL_CreateTextureFromSurface(R,sf);
  SDL_Rect d={x,y,sf->w,sf->h}; if(t){ SDL_RenderCopy(R,t,NULL,&d); SDL_DestroyTexture(t); }
  SDL_FreeSurface(sf); }
static int textw(TTF_Font *f,const char *s){ if(!f||!s) return 0; int w=0,h=0; TTF_SizeUTF8(f,s,&w,&h); return w; }
static void thick_line(int x1,int y1,int x2,int y2,unsigned rgb){
  SDL_SetRenderDrawColor(R,rgb>>16&255,rgb>>8&255,rgb&255,255);
  SDL_RenderDrawLine(R,x1,y1,x2,y2); SDL_RenderDrawLine(R,x1,y1+1,x2,y2+1); }
static void icon_back(int cx,int cy,unsigned rgb){ thick_line(cx+6,cy-7,cx-5,cy,rgb); thick_line(cx-5,cy,cx+6,cy+7,rgb); }
static void icon_fwd(int cx,int cy,unsigned rgb){ thick_line(cx-6,cy-7,cx+5,cy,rgb); thick_line(cx+5,cy,cx-6,cy+7,rgb); }
static void icon_reload(int cx,int cy,unsigned rgb){
  SDL_SetRenderDrawColor(R,rgb>>16&255,rgb>>8&255,rgb&255,255);
  for(int a=40;a<300;a+=8){ double r=a*3.14159265/180.0, r2=(a+8)*3.14159265/180.0;
    SDL_RenderDrawLine(R,cx+(int)(8*cos(r)),cy+(int)(8*sin(r)),cx+(int)(8*cos(r2)),cy+(int)(8*sin(r2))); }
  thick_line(cx+6,cy-8,cx+11,cy-3,rgb); thick_line(cx+6,cy-8,cx+2,cy-4,rgb); }
static void icon_dots(int cx,int cy,unsigned rgb){ fill(cx-1,cy-8,3,3,rgb); fill(cx-1,cy-1,3,3,rgb); fill(cx-1,cy+6,3,3,rgb); }
static void icon_lock(int x,int y,unsigned rgb){ frame(x,y+4,8,7,rgb); fill(x+1,y+5,6,5,rgb);
  SDL_SetRenderDrawColor(R,rgb>>16&255,rgb>>8&255,rgb&255,255);
  SDL_RenderDrawLine(R,x+2,y+4,x+2,y+2); SDL_RenderDrawLine(R,x+6,y+4,x+6,y+2); SDL_RenderDrawLine(R,x+2,y+1,x+6,y+1); }

static int content_h(void){ int h=g_H-TOP-(g_full?0:g_stath); return h>40?h:40; }
static int chrome_hit(int x,int y){
  if(!g_full && y>=TABH&&y<TOP){
    if(x>=6&&x<40) return HIT_BACK; if(x>=42&&x<76) return HIT_FWD; if(x>=78&&x<112) return HIT_RELOAD;
    if(x>=g_W-44) return HIT_MENU; if(x>=118&&x<g_W-52) return HIT_OMNI; }
  return HIT_NONE; }

static void send_hello(void){
  if(g_fd<0) return;
  unsigned char b[5]; asbc_put_u16(b,(unsigned)g_W); asbc_put_u16(b+2,(unsigned)content_h());
  const char *br=getenv("ASB_BROWSER"); /* firefox|chrome: ask the host for that backend */
  b[4]=(unsigned char)(br&&!strcasecmp(br,"firefox")?2:br&&!strcasecmp(br,"chrome")?1:0);
  asbc_send(g_fd,ASBC_HELLO,b,b[4]?5:4); }
static void send_nav(const char *u){
  if(g_fd<0||!u||!u[0]) return;
  uint32_t n=(uint32_t)strlen(u);
  if(u!=g_url) snprintf(g_url,sizeof g_url,"%s",u);
  g_loading=1;
  asbc_send(g_fd,ASBC_NAV,u,n); }
static void url_from_input(const char *in, char *out, int cap){
  if(strstr(in,"://")||!strncmp(in,"about:",6)){ snprintf(out,cap,"%s",in); return; }
  int dot=0; for(const char *p=in;*p;p++) if(*p=='.') dot=1;
  if(dot && !strchr(in,' ')){ snprintf(out,cap,"https://%s",in); return; }
  char q[1200]; int o=0;
  for(const char *p=in;*p&&o<(int)sizeof q-4;p++){
    unsigned char c=(unsigned char)*p;
    if(c==' '){ q[o++]='%'; q[o++]='2'; q[o++]='0'; }
    else q[o++]=(char)c; }
  q[o]=0;
  snprintf(out,cap,"?%s",in); (void)q; /* host substitutes its search engine (asb://settings) */
}

static int tcp_connect(const char *host, int port){
  int s=socket(AF_INET,SOCK_STREAM,0); if(s<0) return -1;
  struct sockaddr_in a; memset(&a,0,sizeof a);
  a.sin_family=AF_INET; a.sin_port=htons((uint16_t)port);
  if(!inet_aton(host,&a.sin_addr)){ close(s); return -1; }
  int fl=fcntl(s,F_GETFL,0); fcntl(s,F_SETFL,fl|O_NONBLOCK);
  int rc=connect(s,(struct sockaddr*)&a,sizeof a);
  if(rc<0 && errno!=EINPROGRESS){ close(s); return -1; }
  fd_set w; FD_ZERO(&w); FD_SET(s,&w);
  struct timeval tv={2,0};
  if(select(s+1,NULL,&w,NULL,&tv)<=0){ close(s); return -1; }
  int err=0; socklen_t el=sizeof err; getsockopt(s,SOL_SOCKET,SO_ERROR,&err,&el);
  if(err){ close(s); return -1; }
  fcntl(s,F_SETFL,fl);
  int one=1; setsockopt(s,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);
  return s; }

/* bounded string copy (truncates, always terminates) */
static void scopy(char *d, size_t cap, const char *s){ size_t n=strlen(s); if(n>=cap) n=cap-1; memcpy(d,s,n); d[n]=0; }
static char g_exe_dir[512]=".";
static char g_token[ASBC_MAX_TOKEN+1];
#define MAXTABS 16
static int g_ntabs, g_atab; static char g_tabt[MAXTABS][64];
static void apply_tabs(const unsigned char *p, int n){
  if(n<2) return; int c=p[0], o=2; g_atab=p[1]; g_ntabs=0;
  for(int i=0;i<c && i<MAXTABS && o+2<=n;i++){
    unsigned l=asbc_get_u16(p+o); o+=2; if(o+(int)l>n) break;
    unsigned m=l<63?l:63; memcpy(g_tabt[i],p+o,m); g_tabt[i][m]=0; o+=(int)l; g_ntabs++; } }
static int tab_w(void){ int n=g_ntabs>0?g_ntabs:1; int tw=(g_W-60)/n; if(tw>240) tw=240; if(tw<60) tw=60; return tw; }
/* tab strip hit: index, or -1; *close=1 when on the tab's x */
static int tab_hit(int x,int y,int *close){
  *close=0; if(y>=TABH) return -1; int tw=tab_w(), n=g_ntabs>0?g_ntabs:1;
  for(int i=0;i<n;i++){ int x0=8+i*(tw+4); if(x>=x0&&x<x0+tw){ if(x>=x0+tw-20) *close=1; return i; } }
  int px=8+n*(tw+4); if(x>=px&&x<px+28) return 1000; /* "+" new tab */
  return -1; }
static pid_t g_spawned=0;
static int is_local(const char *h){ return !strcmp(h,"127.0.0.1")||!strcmp(h,"localhost")||!strcmp(h,"::1"); }
static int spawn_chromed(void){
  if(g_spawned>0 || !is_local(g_host) || getenv("ASB_NOSPAWN")) return -1; /* only once, only for a local host */
  char path[600]; snprintf(path,sizeof path,"%s/asb-chromed",g_exe_dir);
  if(access(path,X_OK)) snprintf(path,sizeof path,"build/host/asb-chromed");
  if(access(path,X_OK)) return -1;
  pid_t pid=fork(); if(pid<0) return -1;
  if(pid==0){ execl(path,path,(char*)0); _exit(127); }
  g_spawned=pid; return 0; }

static int g_replaced;
static int connect_host(void){
  if(g_fd>=0){ close(g_fd); g_fd=-1; }
  int s=tcp_connect(g_host,g_port);
  if(s<0 && spawn_chromed()==0){ for(int i=0;i<40 && s<0;i++){ usleep(150000); s=tcp_connect(g_host,g_port); } }
  if(s<0){ snprintf(g_status,sizeof g_status,"Cannot reach asb-chromed at %s:%d",g_host,g_port); return -1; }
  int hs=asbc_handshake_client(s,g_token);
  if(hs){ close(s); snprintf(g_status,sizeof g_status,hs==-2?"asb-chromed rejected the token":"ASBC handshake failed (old asb-chromed?)"); return -1; }
  g_fd=s; snprintf(g_status,sizeof g_status,"connected");
  send_hello();
  return 0; }

static unsigned char *g_rgba; static int g_rbw, g_rbh;
static void apply_frame(unsigned char *p, int n){
  if(n<14) return;
  int cssw=asbc_get_u16(p), cssh=asbc_get_u16(p+2);
  int x=asbc_get_u16(p+4), y=asbc_get_u16(p+6);
  int tw=asbc_get_u16(p+8), th=asbc_get_u16(p+10);
  int fmt=p[12];
  unsigned char *tile=NULL; int iw=0,ih=0,comp=0;
  if(fmt==2){ qoi_desc d; memset(&d,0,sizeof d); tile=qoi_decode(p+14,n-14,&d,4); iw=d.width; ih=d.height; }
  else { tile=stbi_load_from_memory(p+4,n-4,&iw,&ih,&comp,4); if(tile){ x=0;y=0;tw=iw;th=ih;cssw=iw;cssh=ih; } }
  if(!tile) return;
  if(!g_rgba || g_rbw!=cssw || g_rbh!=cssh){
    free(g_rgba); g_rgba=calloc((size_t)cssw*(size_t)cssh,4);
    g_rbw=cssw; g_rbh=cssh;
    if(g_page) SDL_DestroyTexture(g_page);
    g_page=SDL_CreateTexture(R,SDL_PIXELFORMAT_ABGR8888,SDL_TEXTUREACCESS_STREAMING,cssw,cssh);
    g_pw=cssw; g_ph=cssh;
  }
  if(tw<1) tw=iw; if(th<1) th=ih;
  for(int row=0;row<th && y+row<g_rbh;row++)
    memcpy(g_rgba+((y+row)*g_rbw+x)*4, tile+row*iw*4, (size_t)tw*4);
  free(tile);
  if(g_page) SDL_UpdateTexture(g_page,NULL,g_rgba,g_rbw*4);
}
static void apply_meta(unsigned char *p, int n){
  if(n<5) return;
  int fl=p[0]; g_can_back=!!(fl&1); g_can_fwd=!!(fl&2); g_loading=!!(fl&4);
  int o=1; unsigned ul=asbc_get_u16(p+o); o+=2;
  if(o+(int)ul+2>n) return;
  int m=ul<(unsigned)sizeof g_url-1?(int)ul:(int)sizeof g_url-1;
  memcpy(g_url,p+o,(size_t)m); g_url[m]=0; o+=(int)ul;
  unsigned tl=asbc_get_u16(p+o); o+=2;
  if(o+(int)tl>n) tl=(unsigned)(n-o);
  m=tl<(unsigned)sizeof g_title-1?(int)tl:(int)sizeof g_title-1;
  memcpy(g_title,p+o,(size_t)m); g_title[m]=0;
  if(g_title[0]) SDL_SetWindowTitle(Wn,g_title);
}
static void send_cmd(const char *fmt,...){
  if(g_fd<0) return; char b[2600]; va_list ap; va_start(ap,fmt); vsnprintf(b,sizeof b,fmt,ap); va_end(ap);
  asbc_send(g_fd,ASBC_CMD,b,(uint32_t)strlen(b)); }
/* ---- popup menus (main, context, suggestions) ---- */
#define POPMAX 26
enum { M_NONE, M_NEWTAB, M_REOPEN, M_ZOOMIN, M_ZOOMOUT, M_ZOOM0, M_FIND, M_BOOKMARKS, M_HISTORY, M_DOWNLOADS, M_PASSWORDS,
       M_HOSTSET, M_SAVEPNG, M_SAVEHTML, M_SAVEPDF, M_SOURCE, M_FULL, M_QUIT, M_TABLEFT, M_TABRIGHT,
       M_OPENTAB, M_OPEN, M_COPY, M_SAVEIMG, M_BACK, M_FWD, M_RELOAD, M_BOOKMARK, M_SUGG };
static struct { char label[160]; int id; char arg[1200]; } g_pop[POPMAX];
static int g_popn, g_popkind, g_popx, g_popy, g_popw, g_pophov=-1, g_sugsel=-1, g_quit, g_ctxx, g_ctxy;
static int pop_rowh(void){ return g_uisize+12; }
static void pop_add(const char *l,int id,const char *arg){ if(g_popn>=POPMAX) return; snprintf(g_pop[g_popn].label,sizeof g_pop[0].label,"%s",l); g_pop[g_popn].id=id; snprintf(g_pop[g_popn].arg,sizeof g_pop[0].arg,"%s",arg?arg:""); g_popn++; }
static int textw(TTF_Font *f,const char *s);
static TTF_Font *Fbody, *Fsmall;
static void pop_place(int x,int y,int w){
  if(w<=0){ w=80; for(int i=0;i<g_popn;i++){ int tw=textw(Fbody,g_pop[i].label)+32; if(tw>w) w=tw; } }
  if(w>g_W-8) w=g_W-8; int h=g_popn*pop_rowh();
  if(x+w>g_W-2) x=g_W-2-w; if(x<2) x=2; if(y+h>g_H-2) y=g_H-2-h; if(y<1) y=1;
  g_popx=x; g_popy=y; g_popw=w; g_pophov=-1; }
static int pop_hit(int x,int y){ if(!g_popn||x<g_popx||x>=g_popx+g_popw||y<g_popy||y>=g_popy+g_popn*pop_rowh()) return -1; return (y-g_popy)/pop_rowh(); }
static void pop_close(void){ g_popn=0; g_popkind=0; g_sugsel=-1; }
static void menu_main(int x,int y){
  pop_close(); g_popkind=1;
  pop_add("New tab                 Ctrl+T",M_NEWTAB,NULL); pop_add("Reopen closed tab  Ctrl+Shift+T",M_REOPEN,NULL);
  pop_add("Move tab left",M_TABLEFT,NULL); pop_add("Move tab right",M_TABRIGHT,NULL); pop_add("-",M_NONE,NULL);
  { char z[64]; snprintf(z,sizeof z,"Zoom in  (now %d%%)     Ctrl++",g_zoom); pop_add(z,M_ZOOMIN,NULL); }
  pop_add("Zoom out                Ctrl+-",M_ZOOMOUT,NULL); pop_add("Actual size             Ctrl+0",M_ZOOM0,NULL);
  pop_add(g_full?"Leave full screen          F11":"Full screen                F11",M_FULL,NULL); pop_add("-",M_NONE,NULL);
  pop_add("Find in page            Ctrl+F",M_FIND,NULL); pop_add("Bookmarks         Ctrl+Shift+O",M_BOOKMARKS,NULL);
  pop_add("History                 Ctrl+H",M_HISTORY,NULL); pop_add("Downloads               Ctrl+J",M_DOWNLOADS,NULL);
  pop_add("Passwords",M_PASSWORDS,NULL); pop_add("-",M_NONE,NULL);
  pop_add("Save page as PNG        Ctrl+S",M_SAVEPNG,NULL); pop_add("Save page as HTML",M_SAVEHTML,NULL);
  pop_add("Save page as PDF / print  Ctrl+P",M_SAVEPDF,NULL); pop_add("View source             Ctrl+U",M_SOURCE,NULL);
  pop_add("-",M_NONE,NULL); pop_add("Settings",M_HOSTSET,NULL); pop_add("Quit                    Ctrl+Q",M_QUIT,NULL);
  pop_place(x,y,0); }
static void menu_ctx(const unsigned char *p,int n){
  static char b[3700]; int m=n<(int)sizeof b-1?n:(int)sizeof b-1; memcpy(b,p,(size_t)m); b[m]=0;
  char *l=b, *i=strchr(l,'\n'); if(i) *i++=0; else i="";
  char *sl=strchr(i,'\n'); if(sl) *sl++=0; else sl="";
  pop_close(); g_popkind=2;
  if(l[0]){ pop_add("Open link in new tab",M_OPENTAB,l); pop_add("Open link",M_OPEN,l); pop_add("Copy link address",M_COPY,l); pop_add("-",M_NONE,NULL); }
  if(i[0]){ pop_add("Save image",M_SAVEIMG,i); pop_add("Open image in new tab",M_OPENTAB,i); pop_add("Copy image address",M_COPY,i); pop_add("-",M_NONE,NULL); }
  if(sl[0]){ pop_add("Copy",M_COPY,sl); pop_add("-",M_NONE,NULL); }
  pop_add("Back",M_BACK,NULL); pop_add("Forward",M_FWD,NULL); pop_add("Reload",M_RELOAD,NULL); pop_add("-",M_NONE,NULL);
  pop_add("Bookmark this page",M_BOOKMARK,NULL); pop_add("Save page as PNG",M_SAVEPNG,NULL); pop_add("View source",M_SOURCE,NULL);
  pop_place(g_ctxx,g_ctxy,0); }
static int g_focus;
static void sugg_apply(const unsigned char *p,int n){
  if(g_focus!=1){ return; }
  pop_close(); g_popkind=3;
  const unsigned char *e=p+n;
  while(p<e && g_popn<10){ const unsigned char *nl=p; while(nl<e && *nl!='\n') nl++;
    char row[1400]; int m=(int)(nl-p); if(m>(int)sizeof row-1) m=(int)sizeof row-1; memcpy(row,p,(size_t)m); row[m]=0;
    char *t=strchr(row,'\t'); if(t) *t++=0;
    if(row[0]){ char lab[160]; snprintf(lab,sizeof lab,"%.70s  -  %.80s",t&&t[0]?t:row,row); pop_add(lab,M_SUGG,row); }
    p=nl+1; }
  if(!g_popn){ g_popkind=0; return; }
  pop_place(118,TOP,g_W-118-52); }
static void pump_asbc(void){
  if(g_fd<0) return;
  for(int i=0;i<24;i++){
    int op=0; int n=asbc_recv_nb(g_fd,&op,g_jpeg_buf,g_jpeg_cap);
    if(n==0 && op<0) break;
    if(n<0){ close(g_fd); g_fd=-1; g_replaced=!strncmp(g_status,"replaced",8); snprintf(g_status,sizeof g_status,g_replaced?"replaced by another client - click to reconnect":"Chrome host disconnected"); break; }
    if(op==ASBC_FRAME) apply_frame(g_jpeg_buf,n);
    else if(op==ASBC_META) apply_meta(g_jpeg_buf,n);
    else if(op==ASBC_TABS) apply_tabs(g_jpeg_buf,n);
    else if(op==ASBC_CTX) menu_ctx(g_jpeg_buf,n);
    else if(op==ASBC_SUGG) sugg_apply(g_jpeg_buf,n);
    else if(op==ASBC_ZOOM){ if(n>=2) g_zoom=(int)asbc_get_u16(g_jpeg_buf); }
    else if(op==ASBC_FINDRES){ if(n>=4){ g_findcur=(int)asbc_get_u16(g_jpeg_buf); g_findtot=(int)asbc_get_u16(g_jpeg_buf+2); } }
    else if(op==ASBC_STATUS){ int m=n<(int)sizeof g_status-1?n:(int)sizeof g_status-1; memcpy(g_status,g_jpeg_buf,(size_t)m); g_status[m]=0; }
    else if(op==ASBC_CLIP){
      char *z=malloc((size_t)n+1); if(z){ memcpy(z,g_jpeg_buf,(size_t)n); z[n]=0; SDL_SetClipboardText(z); free(z); }
      snprintf(g_status,sizeof g_status,"copied %d chars",n);
    }
    else if(op==ASBC_CURSOR){
      int c=n?g_jpeg_buf[0]:0;
      SDL_Cursor *want=c==1?cur_ibeam:c==2?cur_hand:c==3?cur_wait:cur_arrow;
      if(want) SDL_SetCursor(want); }
  }
}

static int g_findmode;
static void draw(void){
  fill(0,0,g_W,g_H,CH_BG);
  if(!g_full){
  fill(0,0,g_W,TABH,CH_TABBG);
  { int tw=tab_w(), n=g_ntabs>0?g_ntabs:1;
    for(int i=0;i<n;i++){ int x0=8+i*(tw+4); int act=g_ntabs?i==g_atab:1;
      if(act) fillr(x0,4,tw,TABH-4,CH_TAB,10);
      char tt[80]; scopy(tt,sizeof tt,act&&g_title[0]?g_title:(g_ntabs&&g_tabt[i][0]?g_tabt[i]:"New Tab"));
      while(strlen(tt)>3 && textw(Fsmall,tt)>tw-34) tt[strlen(tt)-1]=0;
      text(Fsmall,x0+12,(TABH-TTF_FontHeight(Fsmall))/2+2,tt,act?CH_FG:CH_MUTED);
      text(Fsmall,x0+tw-16,(TABH-TTF_FontHeight(Fsmall))/2+2,"x",CH_MUTED); }
    text(Fbody,8+n*(tw+4)+8,(TABH-TTF_FontHeight(Fbody))/2+2,"+",CH_FG); }
  fill(0,TABH,g_W,TOOLH,CH_TOOL);
  fill(0,TOP-1,g_W,1,CH_LINE);
  unsigned on=CH_FG, off=CH_MUTED;
  icon_back(22,TABH+TOOLH/2,g_can_back?on:off);
  icon_fwd(58,TABH+TOOLH/2,g_can_fwd?on:off);
  if(g_loading){ int cx=94,cy=TABH+TOOLH/2; thick_line(cx-6,cy-6,cx+6,cy+6,on); thick_line(cx+6,cy-6,cx-6,cy+6,on); }
  else icon_reload(94,TABH+TOOLH/2,on);
  icon_dots(g_W-24,TABH+TOOLH/2,on);
  { int ox=118, oh=TOOLH-14, oy=TABH+7, ow=g_W-118-52; if(ow<80) ow=80;
    unsigned oc=g_focus==1?CH_ACCENT:CH_LINE;
    fillr(ox,oy,ow,oh,CH_OMNI,oh/2); frame(ox,oy,ow,oh,oc);
    int tx=ox+12;
    if(g_focus==1 && g_findmode){ text(Fsmall,ox+10,oy+(oh-TTF_FontHeight(Fsmall))/2,"Find:",CH_ACCENT); tx=ox+14+textw(Fsmall,"Find:"); }
    else if(!strncmp(g_url,"https://",8)||!strncmp(g_url,"asb://",6)){ icon_lock(ox+10,oy+oh/2-6,0x188038); tx=ox+24; }
    else if(!strncmp(g_url,"http://",7)){ text(Fsmall,ox+8,oy+(oh-TTF_FontHeight(Fsmall))/2,"Not secure",0xC5221F); tx=ox+14+textw(Fsmall,"Not secure"); }
    const char *ub=g_focus==1?g_edit:g_url;
    char rt[40]=""; if(g_focus==1 && g_findmode){ if(g_findtot) snprintf(rt,sizeof rt,"%d of %d",g_findcur,g_findtot); else if(g_edit[0]) snprintf(rt,sizeof rt,"no matches"); }
    else if(g_zoom!=100) snprintf(rt,sizeof rt,"%d%%",g_zoom);
    int rw=rt[0]?textw(Fsmall,rt)+16:0;
    char show[2400]; snprintf(show,sizeof show,"%s",ub);
    while(strlen(show)>8 && textw(Fbody,show)>ow-(tx-ox)-16-rw) show[strlen(show)-1]=0;
    text(Fbody,tx,oy+(oh-TTF_FontHeight(Fbody))/2,show,CH_FG);
    if(rt[0]) text(Fsmall,ox+ow-rw,oy+(oh-TTF_FontHeight(Fsmall))/2,rt,CH_MUTED); }
  if(g_loading){ int seg=g_W/5, x=(int)((SDL_GetTicks()/4)%(unsigned)(g_W+seg))-seg; fill(x<0?0:x,TOP-3,x<0?seg+x:seg,3,CH_ACCENT); }
  }
  int vh=content_h();
  if(g_page){ SDL_Rect d={0,TOP,g_W,vh}; SDL_RenderCopy(R,g_page,NULL,&d); }
  else { text(Fbody,24,TOP+24,g_fd<0?"Start asb-chromed on the Linux host":"Waiting for first frame…",CH_MUTED); }
  if(!g_full){ fill(0,g_H-g_stath,g_W,g_stath,0xF8F9FA); fill(0,g_H-g_stath,g_W,1,CH_LINE);
    text(Fsmall,8,g_H-g_stath+(g_stath-TTF_FontHeight(Fsmall))/2,g_status[0]?g_status:g_url,CH_MUTED); }
  if(g_popn){ int rh=pop_rowh(), h=g_popn*rh;
    fill(g_popx+3,g_popy+3,g_popw,h,0xB0B0B0); fill(g_popx,g_popy,g_popw,h,0xFFFFFF); frame(g_popx,g_popy,g_popw,h,CH_LINE);
    for(int i=0;i<g_popn;i++){ int y=g_popy+i*rh; int hov=(g_popkind==3?i==g_sugsel:i==g_pophov)&&g_pop[i].id!=M_NONE;
      if(g_pop[i].id==M_NONE){ fill(g_popx+8,y+rh/2,g_popw-16,1,CH_LINE); continue; }
      if(hov) fill(g_popx+1,y,g_popw-2,rh,0xE8F0FE);
      char t[200]; scopy(t,sizeof t,g_pop[i].label); while(strlen(t)>4 && textw(Fbody,t)>g_popw-24) t[strlen(t)-1]=0;
      text(Fbody,g_popx+12,y+(rh-TTF_FontHeight(Fbody))/2,t,CH_FG); } }
  SDL_RenderPresent(R);
}

static void do_menu(int id,const char *arg){
  switch(id){
  case M_NEWTAB: if(g_fd>=0) asbc_send(g_fd,ASBC_TABNEW,NULL,0); break;
  case M_REOPEN: send_cmd("reopen"); break;
  case M_TABLEFT: if(g_atab>0) send_cmd("tabmove %d %d",g_atab,g_atab-1); break;
  case M_TABRIGHT: if(g_atab+1<g_ntabs) send_cmd("tabmove %d %d",g_atab,g_atab+1); break;
  case M_ZOOMIN: send_cmd("zoom +"); break; case M_ZOOMOUT: send_cmd("zoom -"); break; case M_ZOOM0: send_cmd("zoom 0"); break;
  case M_FULL: g_full=!g_full; SDL_SetWindowFullscreen(Wn,g_full?SDL_WINDOW_FULLSCREEN_DESKTOP:0); send_hello(); break;
  case M_FIND: g_focus=1; g_findmode=1; g_omni_sel=0; g_edit[0]=0; break;
  case M_BOOKMARKS: send_cmd("page bookmarks"); break; case M_HISTORY: send_cmd("page history"); break;
  case M_DOWNLOADS: send_cmd("page downloads"); break; case M_PASSWORDS: send_cmd("page passwords"); break;
  case M_HOSTSET: send_cmd("page settings"); break;
  case M_SAVEPNG: send_cmd("savepng"); break; case M_SAVEHTML: send_cmd("savehtml"); break; case M_SAVEPDF: send_cmd("savepdf"); break;
  case M_SOURCE: send_cmd("source"); break; case M_QUIT: g_quit=1; break;
  case M_OPENTAB: send_cmd("newtab %s",arg); break;
  case M_OPEN: case M_SUGG: send_nav(arg); break;
  case M_COPY: SDL_SetClipboardText(arg); snprintf(g_status,sizeof g_status,"Copied"); break;
  case M_SAVEIMG: send_cmd("saveimg %s",arg); break;
  case M_BACK: if(g_fd>=0) asbc_send(g_fd,ASBC_BACK,NULL,0); break;
  case M_FWD: if(g_fd>=0) asbc_send(g_fd,ASBC_FWD,NULL,0); break;
  case M_RELOAD: if(g_fd>=0) asbc_send(g_fd,ASBC_RELOAD,NULL,0); break;
  case M_BOOKMARK: send_cmd("bookmark"); break;
  default: break; } }

static int vk_of(SDL_Keycode k){
  if(k>=SDLK_a&&k<=SDLK_z) return (int)('A'+(k-SDLK_a));
  if(k>=SDLK_0&&k<=SDLK_9) return (int)k;
  switch(k){
    case SDLK_RETURN: case SDLK_KP_ENTER: return 13;
    case SDLK_BACKSPACE: return 8;
    case SDLK_TAB: return 9;
    case SDLK_ESCAPE: return 27;
    case SDLK_LEFT: return 37;
    case SDLK_UP: return 38;
    case SDLK_RIGHT: return 39;
    case SDLK_DOWN: return 40;
    case SDLK_DELETE: return 46;
    case SDLK_HOME: return 36;
    case SDLK_END: return 35;
    case SDLK_PAGEUP: return 33;
    case SDLK_PAGEDOWN: return 34;
    default: return (int)k; }
}
static int mods_of(SDL_Keymod m){
  int r=0; if(m&(KMOD_ALT)) r|=1; if(m&(KMOD_CTRL)) r|=2; if(m&(KMOD_SHIFT)) r|=8; if(m&(KMOD_GUI)) r|=4; return r; }
static void send_key(int ty, int vk, int mods, const char *text){
  if(g_fd<0) return;
  unsigned char b[80]; int n=text?(int)strlen(text):0; if(n>64) n=64;
  b[0]=(unsigned char)ty; asbc_put_u16(b+1,(unsigned)vk); b[3]=(unsigned char)mods; asbc_put_u16(b+4,(unsigned)n);
  if(n) memcpy(b+6,text,(size_t)n);
  asbc_send(g_fd,ASBC_KEY,b,(uint32_t)(6+n)); }
static int g_last_btn;
static void send_mouse(int x,int y,int buttons,int ty){
  if(g_fd<0) return; y-=TOP; if(y<0) return;
  int cw=g_W, ch=content_h(); if(ch<1) ch=1; if(cw<1) cw=1;
  int mx=x, my=y;
  if(g_pw>0 && g_ph>0){ mx=(int)((long)x*g_pw/cw); my=(int)((long)y*g_ph/ch); }
  if(mx<0) mx=0; if(my<0) my=0;
  if(buttons) g_last_btn=buttons;
  int btn=buttons?buttons: (ty==ASBC_MUP?g_last_btn:0);
  unsigned char b[6]; asbc_put_u16(b,(unsigned)mx); asbc_put_u16(b+2,(unsigned)my); b[4]=(unsigned char)btn; b[5]=(unsigned char)ty;
  asbc_send(g_fd,ASBC_MOUSE,b,6); }

static void save_shot(const char *path){
  SDL_Surface*s=SDL_CreateRGBSurfaceWithFormat(0,g_W,g_H,32,SDL_PIXELFORMAT_ARGB8888);
  SDL_RenderReadPixels(R,NULL,SDL_PIXELFORMAT_ARGB8888,s->pixels,s->pitch); SDL_SaveBMP(s,path); SDL_FreeSurface(s); }

int main(int argc, char **argv){
  if(getenv("ASB_TOKEN")) snprintf(g_token,sizeof g_token,"%s",getenv("ASB_TOKEN"));
  const char *shot=NULL,*type=NULL, *cmds[8]; int ncmds=0, cmdsent=0; int quit_ms=0, clickx=-1, clicky=-1, clicked=0, typed=0;
  { char b[1024]; ssize_t n=readlink("/proc/self/exe",b,sizeof b-1); if(n>0){ b[n]=0; char *sl=strrchr(b,'/'); if(sl){ *sl=0; scopy(g_exe_dir,sizeof g_exe_dir,b); } } }
  for(int i=1;i<argc;i++){
    if(!strcmp(argv[i],"-h")||!strcmp(argv[i],"--help")){ printf("usage: asb-sdl [-host H] [-port P] [-token T] [-W w] [-H h] [-shot file.png] [-quit ms] [-click x,y] [-type text] [url]\n  ASB_TOKEN, ASB_NOSPAWN=1 (never start a local asb-chromed)\n"); return 0; }
    if(!strcmp(argv[i],"-W")&&i+1<argc) g_W=atoi(argv[++i]);
    else if(!strcmp(argv[i],"-H")&&i+1<argc) g_H=atoi(argv[++i]);
    else if(!strcmp(argv[i],"-shot")&&i+1<argc) shot=argv[++i];
    else if(!strcmp(argv[i],"-quit")&&i+1<argc) quit_ms=atoi(argv[++i]);
    else if(!strcmp(argv[i],"-host")&&i+1<argc) snprintf(g_host,sizeof g_host,"%s",argv[++i]);
    else if(!strcmp(argv[i],"-port")&&i+1<argc) g_port=atoi(argv[++i]);
    else if(!strcmp(argv[i],"-token")&&i+1<argc) snprintf(g_token,sizeof g_token,"%s",argv[++i]);
    else if(!strcmp(argv[i],"-click")&&i+1<argc){ sscanf(argv[++i],"%d,%d",&clickx,&clicky); }
    else if(!strcmp(argv[i],"-type")&&i+1<argc){ type=argv[++i]; }
    else if(!strcmp(argv[i],"-uisize")&&i+1<argc){ g_uisize=atoi(argv[++i]); if(g_uisize<10||g_uisize>40) g_uisize=16; }
    else if(!strcmp(argv[i],"-large")) g_uisize=22;
    else if(!strcmp(argv[i],"-uifont")&&i+1<argc) setenv("ASB_UIFONT",argv[++i],1);
    else if(!strcmp(argv[i],"-full")) g_full=1;
    else if(!strcmp(argv[i],"-cmd")&&i+1<argc){ if(ncmds<8) cmds[ncmds++]=argv[++i]; else i++; }
    else if(argv[i][0]!='-') snprintf(g_url,sizeof g_url,"%s",argv[i]);
  }
  g_jpeg_buf=malloc(g_jpeg_cap); if(!g_jpeg_buf) return 20;
  if(SDL_Init(SDL_INIT_VIDEO)||TTF_Init()){ fprintf(stderr,"SDL: %s\n",SDL_GetError()); return 20; }
  if(getenv("ASB_UISIZE")) g_uisize=atoi(getenv("ASB_UISIZE")); if(g_uisize<10||g_uisize>40) g_uisize=16;
  Fbody=open_font(g_uisize); Fsmall=open_font(g_uisize*13/16);
  g_tabh=g_uisize*36/16; g_toolh=g_uisize*44/16; g_stath=g_uisize*24/16;
  cur_arrow=SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
  cur_hand=SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
  cur_wait=SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAITARROW);
  cur_ibeam=SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
  Uint32 wf=SDL_WINDOW_RESIZABLE; if(shot) wf|=SDL_WINDOW_BORDERLESS;
  Wn=SDL_CreateWindow("AmiSurf (Chrome)",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,g_W,g_H,wf);
  R=SDL_CreateRenderer(Wn,-1,SDL_RENDERER_SOFTWARE);
  connect_host();
  if(g_fd>=0 && g_url[0]) send_nav(g_url);
  SDL_StartTextInput();
  draw();
  Uint32 t0=SDL_GetTicks(); int running=1, dirty=1, dragtab=-1, popdown=0; (void)dirty;
  while(running){
    if(quit_ms>0 && SDL_GetTicks()-t0>(Uint32)quit_ms) running=0;
    SDL_Event e;
    while(SDL_PollEvent(&e)){
      if(e.type==SDL_QUIT) running=0;
      else if(e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_SIZE_CHANGED){
        g_W=e.window.data1; g_H=e.window.data2; send_hello(); dirty=1; }
      else if(e.type==SDL_MOUSEMOTION){
        if(g_popn && g_popkind!=3){ int r=pop_hit(e.motion.x,e.motion.y); g_pophov=r; if(r>=0) continue; }
        int ch=chrome_hit(e.motion.x,e.motion.y);
        SDL_Cursor *want=ch==HIT_OMNI?cur_ibeam:(ch?cur_hand:cur_arrow);
        if(want) SDL_SetCursor(want);
        if(e.motion.y>=TOP){
          static Uint32 lastmv; Uint32 now=SDL_GetTicks();
          int dragging=e.motion.state&SDL_BUTTON_LMASK;
          if(dragging || now-lastmv>=40){ lastmv=now; send_mouse(e.motion.x,e.motion.y,e.motion.state,ASBC_MMOVE); }
        }
      }
      else if(e.type==SDL_MOUSEBUTTONDOWN){
        if(g_fd<0 && g_replaced){ g_replaced=0; continue; }
        if(g_popn){ int r=pop_hit(e.button.x,e.button.y);
          if(r>=0){ if(e.button.button==SDL_BUTTON_LEFT && g_pop[r].id!=M_NONE){ int id=g_pop[r].id; char a[1200]; scopy(a,sizeof a,g_pop[r].arg); pop_close(); g_focus=0; do_menu(id,a); popdown=1; } continue; }
          pop_close(); popdown=1; if(!(e.button.button==SDL_BUTTON_RIGHT && e.button.y>=TOP)) continue; }
        if(e.button.button==SDL_BUTTON_RIGHT){
          if(e.button.y>=TOP && e.button.y<TOP+content_h()){ g_ctxx=e.button.x; g_ctxy=e.button.y;
            int cx=g_pw>0?(int)((long)e.button.x*g_pw/g_W):e.button.x, cy=g_ph>0?(int)((long)(e.button.y-TOP)*g_ph/content_h()):e.button.y-TOP;
            send_cmd("ctx %d %d",cx,cy); }
          continue; }
        if(!g_full){ int cl, th=tab_hit(e.button.x,e.button.y,&cl);
          if(th==1000){ if(g_fd>=0) asbc_send(g_fd,ASBC_TABNEW,NULL,0); g_focus=1; g_findmode=0; g_omni_sel=0; g_edit[0]=0; SDL_StartTextInput(); continue; }
          if(th>=0 && g_fd>=0){ unsigned char ix=(unsigned char)th;
            if(cl||e.button.button==SDL_BUTTON_MIDDLE) asbc_send(g_fd,ASBC_TABCLS,&ix,1); else dragtab=th;
            continue; } }
        int ch=chrome_hit(e.button.x,e.button.y);
        if(getenv("ASB_DEBUG")) fprintf(stderr,"asb-sdl: click %d,%d hit %d\n",e.button.x,e.button.y,ch);
        if(ch==HIT_BACK){ if(g_fd>=0) asbc_send(g_fd,ASBC_BACK,NULL,0); }
        else if(ch==HIT_FWD){ if(g_fd>=0) asbc_send(g_fd,ASBC_FWD,NULL,0); }
        else if(ch==HIT_RELOAD){
          if(g_loading){ if(g_fd>=0) asbc_send(g_fd,ASBC_STOP,NULL,0); }
          else if(g_fd>=0) asbc_send(g_fd,ASBC_RELOAD,NULL,0); }
        else if(ch==HIT_OMNI){ g_focus=1; g_findmode=0; g_omni_sel=1; snprintf(g_edit,sizeof g_edit,"%s",g_url); SDL_StartTextInput(); dirty=1; }
        else if(ch==HIT_MENU){ menu_main(g_W-8,TOP); }
        else if(e.button.y>=TOP){
          g_focus=0; SDL_StartTextInput();
          int btn=e.button.button==SDL_BUTTON_LEFT?1:4;
          send_mouse(e.button.x,e.button.y,btn,ASBC_MMOVE);
          send_mouse(e.button.x,e.button.y,btn,ASBC_MDOWN); }
      }
      else if(e.type==SDL_MOUSEBUTTONUP){
        if(popdown){ popdown=0; continue; }
        if(dragtab>=0){ int from=dragtab, cl; dragtab=-1; int to=tab_hit(e.button.x,e.button.y,&cl);
          if(to>=0 && to<1000 && to!=from) send_cmd("tabmove %d %d",from,to);
          else { unsigned char ix=(unsigned char)from; if(g_fd>=0) asbc_send(g_fd,ASBC_TABSEL,&ix,1); }
          continue; }
        if(e.button.button==SDL_BUTTON_RIGHT) continue;
        if(e.button.y>=TOP){
          int btn=e.button.button==SDL_BUTTON_LEFT?1:4;
          send_mouse(e.button.x,e.button.y,btn,ASBC_MUP); }
      }
      else if(e.type==SDL_MOUSEWHEEL){
        if((SDL_GetModState()&KMOD_CTRL)){ send_cmd(e.wheel.y>0?"zoom +":"zoom -"); continue; }
        if(g_fd>=0){ unsigned char b[2]; asbc_put_i16(b,-e.wheel.y*120); asbc_send(g_fd,ASBC_WHEEL,b,2); }
      }
      else if(e.type==SDL_TEXTINPUT){
        if(g_focus==1){
          if(g_omni_sel){ g_edit[0]=0; g_omni_sel=0; }
          size_t l=strlen(g_edit); snprintf(g_edit+l,sizeof g_edit-l,"%s",e.text.text); dirty=1;
          if(!g_findmode) send_cmd("suggest %s",g_edit);
        } else send_key(ASBC_KCHAR,0,0,e.text.text);
      }
      else if(e.type==SDL_KEYDOWN){
        SDL_Keycode k=e.key.keysym.sym; int mods=mods_of(e.key.keysym.mod|SDL_GetModState());
        int ctrl=mods&2, shift=mods&8, alt=mods&1;
        if(getenv("ASB_DEBUG")) fprintf(stderr,"asb-sdl: key %d mods %d\n",(int)k,mods);
        if(k==SDLK_ESCAPE && g_popn){ pop_close(); continue; }
        if(g_focus==1 && g_popkind==3 && g_popn && (k==SDLK_DOWN||k==SDLK_UP)){
          g_sugsel=k==SDLK_DOWN?(g_sugsel+1)%g_popn:(g_sugsel<=0?g_popn-1:g_sugsel-1);
          scopy(g_edit,sizeof g_edit,g_pop[g_sugsel].arg); g_omni_sel=0; continue; }
        if(k==SDLK_F11){ do_menu(M_FULL,NULL); continue; }
        if(k==SDLK_ESCAPE && g_full && g_focus!=1){ do_menu(M_FULL,NULL); continue; }
        if(ctrl && k==SDLK_l){ g_focus=1; g_findmode=0; g_omni_sel=1; snprintf(g_edit,sizeof g_edit,"%s",g_url); SDL_StartTextInput(); dirty=1; continue; }
        if(ctrl && k==SDLK_c){ if(g_focus==1) SDL_SetClipboardText(g_edit); else if(g_fd>=0) asbc_send(g_fd,ASBC_COPY,NULL,0); continue; }
        if(ctrl && k==SDLK_v){
          char *t=SDL_GetClipboardText();
          if(g_focus==1 && t){
            if(g_omni_sel){ snprintf(g_edit,sizeof g_edit,"%s",t); g_omni_sel=0; }
            else { size_t l=strlen(g_edit); snprintf(g_edit+l,sizeof g_edit-l,"%s",t); }
            dirty=1;
          } else if(t && t[0] && g_fd>=0) asbc_send(g_fd,ASBC_PASTE,t,(uint32_t)strlen(t));
          if(t) SDL_free(t);
          continue; }
        if(ctrl && k==SDLK_d){ send_cmd("bookmark"); continue; }
        if(ctrl && k==SDLK_f){ g_focus=1; g_findmode=1; g_omni_sel=0; g_edit[0]=0; pop_close(); SDL_StartTextInput(); snprintf(g_status,sizeof g_status,"Find in page: Enter = next, Shift+Enter = previous, Esc = close"); continue; }
        if((ctrl && k==SDLK_g) || k==SDLK_F3){ send_cmd(shift?"findprev":"findnext"); continue; }
        if(ctrl && shift && k==SDLK_t){ send_cmd("reopen"); continue; }
        if(ctrl && k==SDLK_t){ if(g_fd>=0) asbc_send(g_fd,ASBC_TABNEW,NULL,0); g_focus=1; g_findmode=0; g_omni_sel=0; g_edit[0]=0; SDL_StartTextInput(); continue; }
        if(ctrl && k==SDLK_w){ unsigned char z=(unsigned char)g_atab; if(g_fd>=0) asbc_send(g_fd,ASBC_TABCLS,&z,1); continue; }
        if(ctrl && shift && k==SDLK_PAGEUP){ do_menu(M_TABLEFT,NULL); continue; }
        if(ctrl && shift && k==SDLK_PAGEDOWN){ do_menu(M_TABRIGHT,NULL); continue; }
        if(ctrl && (k==SDLK_TAB||k==SDLK_PAGEDOWN||k==SDLK_PAGEUP)){ send_cmd((k==SDLK_PAGEUP||(k==SDLK_TAB&&shift))?"tabprev":"tabnext"); continue; }
        if(ctrl && k>=SDLK_1 && k<=SDLK_8){ unsigned char z=(unsigned char)(k-SDLK_1); if(z<g_ntabs && g_fd>=0) asbc_send(g_fd,ASBC_TABSEL,&z,1); continue; }
        if(ctrl && (k==SDLK_PLUS||k==SDLK_EQUALS||k==SDLK_KP_PLUS)){ send_cmd("zoom +"); continue; }
        if(ctrl && (k==SDLK_MINUS||k==SDLK_KP_MINUS)){ send_cmd("zoom -"); continue; }
        if(ctrl && (k==SDLK_0||k==SDLK_KP_0)){ send_cmd("zoom 0"); continue; }
        if(ctrl && k==SDLK_u){ send_cmd("source"); continue; }
        if(ctrl && k==SDLK_s){ send_cmd("savepng"); continue; }
        if(ctrl && k==SDLK_p){ send_cmd("savepdf"); continue; }
        if(ctrl && k==SDLK_j){ send_cmd("page downloads"); continue; }
        if(ctrl && k==SDLK_h){ send_cmd("page history"); continue; }
        if(ctrl && shift && k==SDLK_o){ send_cmd("page bookmarks"); continue; }
        if(ctrl && k==SDLK_q){ g_quit=1; continue; }
        if((ctrl && k==SDLK_r) || k==SDLK_F5){ if(g_fd>=0) asbc_send(g_fd,ASBC_RELOAD,NULL,0); continue; }
        if((alt && k==SDLK_LEFT) || (ctrl && k==SDLK_LEFTBRACKET)){ if(g_fd>=0) asbc_send(g_fd,ASBC_BACK,NULL,0); continue; }
        if((alt && k==SDLK_RIGHT) || (ctrl && k==SDLK_RIGHTBRACKET)){ if(g_fd>=0) asbc_send(g_fd,ASBC_FWD,NULL,0); continue; }
        if(alt && k==SDLK_HOME){ send_cmd("home"); continue; }
        if(g_focus==1){
          if(k==SDLK_ESCAPE){ if(g_findmode) send_cmd("findclose"); g_focus=0; g_findmode=0; pop_close(); SDL_StartTextInput(); dirty=1; }
          else if(k==SDLK_RETURN && g_findmode){ if(g_fd>=0){ if(shift) send_cmd("findprev"); else asbc_send(g_fd,ASBC_FIND,g_edit,(uint32_t)strlen(g_edit)); } }
          else if(k==SDLK_RETURN){ char u[2400]; url_from_input(g_edit,u,sizeof u); g_focus=0; pop_close(); SDL_StartTextInput(); send_nav(u); dirty=1; }
          else if(k==SDLK_BACKSPACE){
            if(g_omni_sel){ g_edit[0]=0; g_omni_sel=0; }
            else { size_t l=strlen(g_edit); while(l>0){ l--; if(((unsigned char)g_edit[l]&0xC0)!=0x80) break; } g_edit[l]=0; }
            if(!g_findmode) send_cmd("suggest %s",g_edit);
            dirty=1; }
          continue;
        }
        send_key(ASBC_KDOWN,vk_of(k),mods,NULL);
      }
      else if(e.type==SDL_KEYUP){
        if(g_focus!=1) send_key(ASBC_KUP,vk_of(e.key.keysym.sym),mods_of(e.key.keysym.mod),NULL);
      }
    }
    if(g_quit) running=0;
    pump_asbc();
    if(g_fd<0 && !g_replaced){ static Uint32 last; if(SDL_GetTicks()-last>2000){ last=SDL_GetTicks(); connect_host(); } }
    draw();
    if(clickx>=0 && !clicked && g_page && SDL_GetTicks()-t0>1500){
      send_mouse(clickx,clicky+TOP,1,ASBC_MMOVE);
      send_mouse(clickx,clicky+TOP,1,ASBC_MDOWN); send_mouse(clickx,clicky+TOP,1,ASBC_MUP); clicked=1; }
    if(type && !typed && (clickx<0||clicked) && SDL_GetTicks()-t0>2200){
      for(const char *s=type;*s;s++){ char c[8]; c[0]=*s; c[1]=0; send_key(ASBC_KCHAR,0,0,c); }
      typed=1; }
    if(ncmds && cmdsent<ncmds && g_page && SDL_GetTicks()-t0>(Uint32)(2500+cmdsent*1500)){
      const char *c=cmds[cmdsent++];
      if(!strncmp(c,"ctx ",4)){ sscanf(c+4,"%d %d",&g_ctxx,&g_ctxy); int cx=g_pw>0?(int)((long)g_ctxx*g_pw/g_W):g_ctxx, cy=g_ph>0?(int)((long)(g_ctxy-TOP)*g_ph/content_h()):g_ctxy-TOP; send_cmd("ctx %d %d",cx,cy); }
      else if(!strcmp(c,"menu")) menu_main(g_W-8,TOP);
      else if(!strncmp(c,"find ",5)){ g_focus=1; g_findmode=1; scopy(g_edit,sizeof g_edit,c+5); if(g_fd>=0) asbc_send(g_fd,ASBC_FIND,g_edit,(uint32_t)strlen(g_edit)); }
      else if(!strncmp(c,"type ",5)){ g_focus=1; g_findmode=0; scopy(g_edit,sizeof g_edit,c+5); send_cmd("suggest %s",g_edit); }
      else send_cmd("%s",c); }
    if(shot && SDL_GetTicks()-t0>(Uint32)(quit_ms>200?quit_ms-150:400)){ save_shot(shot); shot=NULL; }
    SDL_Delay(16);
  }
  if(g_fd>=0) close(g_fd);
  return 0;
}
