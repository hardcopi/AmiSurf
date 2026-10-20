/* asb-chromed — Linux Chrome CDP daemon, ASBC on 127.0.0.1:9223 */
#define _GNU_SOURCE
#include <sys/time.h>
#include "asbc.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#include "../third_party/stb/stb_image.h"
#define QOI_IMPLEMENTATION
#define QOI_NO_STDIO
#include "qoi.h"
#include <arpa/inet.h>
#include <limits.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static pid_t g_chrome;
static int g_ws=-1, g_listen=-1, g_cli=-1, g_dbg=9222;
/* 0.85: a second CDP connection to the browser target (/json/version). The page connection (g_ws) never sees
   Browser.download* events or tabs Chrome opens by itself (target=_blank, window.open): both arrive here.
   Not g_bws: the AmiStudio Host's app-mode patches (engine-patches 0002+) have their own browser connection by that
   name (autoAttach for popups, allowAndName downloads). App mode has no tabs: this one must stay off there. */
static int g_bcon=-1; static long long g_bcon_try, g_tabs_due; static char g_tab_mine[80];
static int g_cdp_id=1, g_vw=1280, g_vh=720;
static int g_screencast_on;
static char g_url[2400]="about:blank", g_title[256]="New Tab";
static int g_loading;
static char g_findq[400];
static char g_dlpath[512];
#define MAXTAB 8
static struct { char id[80], ws[300], url[2400], title[128]; } g_tabs[MAXTAB];
static int g_ntab, g_tab;
static unsigned char *g_prev; static int g_prev_w, g_prev_h;
static int g_zoom; static char g_order[MAXTAB][80]; static int g_norder;
static void hist_note(void); static void display_url(const char *in,char *out,int cap); static void feat_enable(void);
static int g_bidi; static char g_ctx[80]; /* Firefox backend (bidi.inc) */
static void bidi_from_cdp(const char *cmd); static int bidi_tree(void); static int bidi_tab_create(const char *url); static void bidi_tab_close(const char *ctx);

/* ---- growable buffer ---- */
typedef struct { char *p; int n, cap; } buf_t;
static void buf_need(buf_t *b, int n){
  if(n<=b->cap) return;
  int c=b->cap?b->cap:4096; while(c<n) c*=2;
  char *q=realloc(b->p,(size_t)c); if(!q){ perror("realloc"); exit(1); }
  b->p=q; b->cap=c; }
/* bounded string copy (truncates, always terminates) */
static void scopy(char *d, size_t cap, const char *s){ size_t n=strlen(s); if(n>=cap) n=cap-1; memcpy(d,s,n); d[n]=0; }

static int wr_all(int fd, const void *buf, size_t n){
  const unsigned char *p=buf;
  while(n){ ssize_t k=write(fd,p,n); if(k<0){ if(errno==EINTR) continue; return -1; } if(!k) return -1; p+=k; n-=(size_t)k; }
  return 0; }
static int rd_all(int fd, void *buf, size_t n){
  unsigned char *p=buf;
  while(n){ ssize_t k=read(fd,p,n); if(k<0){ if(errno==EINTR) continue; return -1; } if(!k) return -2; p+=k; n-=(size_t)k; }
  return 0; }

/* ---- base64 ---- */
static int b64v(int c){
  if(c>='A'&&c<='Z') return c-'A'; if(c>='a'&&c<='z') return c-'a'+26;
  if(c>='0'&&c<='9') return c-'0'+52; if(c=='+') return 62; if(c=='/') return 63; return -1; }
static int b64dec(const char *s, int n, unsigned char *o, int cap){
  int i=0, out=0, v=0, bits=0;
  for(;i<n;i++){
    int x=b64v((unsigned char)s[i]); if(x<0) continue;
    v=(v<<6)|x; bits+=6;
    if(bits>=8){ bits-=8; if(out<cap) o[out++]=(unsigned char)((v>>bits)&255); }
  }
  return out; }

/* ---- JSON string field (first "key":"...") ---- */
static int json_str(const char *j, const char *key, char *out, int cap){
  char pat[80]; snprintf(pat,sizeof pat,"\"%s\"",key);
  const char *p=strstr(j,pat); if(!p) return 0;
  p=strchr(p+strlen(pat),':'); if(!p) return 0;
  p++; while(*p==' '||*p=='\t') p++;
  if(*p!='"') return 0; p++;
  int n=0;
  while(*p && *p!='"' && n<cap-1){
    if(*p=='\\' && p[1]){ p++;
      if(*p=='n') out[n++]='\n'; else if(*p=='r') out[n++]='\r'; else if(*p=='t') out[n++]='\t';
      else if(*p=='u' && p[1]&&p[2]&&p[3]&&p[4]){ /* \uXXXX -> UTF-8 (surrogate pairs combined) */
        unsigned cp=(unsigned)strtoul((char[5]){p[1],p[2],p[3],p[4],0},NULL,16); p+=4;
        if(cp>=0xD800&&cp<0xDC00&&p[1]=='\\'&&p[2]=='u'&&p[3]&&p[4]&&p[5]&&p[6]){
          unsigned lo=(unsigned)strtoul((char[5]){p[3],p[4],p[5],p[6],0},NULL,16);
          if(lo>=0xDC00&&lo<0xE000){ cp=0x10000+((cp-0xD800)<<10)+(lo-0xDC00); p+=6; } }
        if(cp<0x80){ if(n<cap-1) out[n++]=(char)cp; }
        else if(cp<0x800){ if(n<cap-2){ out[n++]=(char)(0xC0|cp>>6); out[n++]=(char)(0x80|(cp&63)); } }
        else if(cp<0x10000){ if(n<cap-3){ out[n++]=(char)(0xE0|cp>>12); out[n++]=(char)(0x80|(cp>>6&63)); out[n++]=(char)(0x80|(cp&63)); } }
        else if(n<cap-4){ out[n++]=(char)(0xF0|cp>>18); out[n++]=(char)(0x80|(cp>>12&63)); out[n++]=(char)(0x80|(cp>>6&63)); out[n++]=(char)(0x80|(cp&63)); } }
      else out[n++]=*p;
      p++; continue; }
    out[n++]=*p++; }
  out[n]=0; return n; }
static int json_int(const char *j, const char *key, int def){
  char pat[80]; snprintf(pat,sizeof pat,"\"%s\"",key);
  const char *p=strstr(j,pat); if(!p) return def;
  p=strchr(p+strlen(pat),':'); if(!p) return def;
  return atoi(p+1); }

/* extract "data":"<base64>" without copying as C string of whole JSON */
static int json_b64_field(const char *j, const char *key, unsigned char *o, int cap){
  char pat[80]; snprintf(pat,sizeof pat,"\"%s\":\"",key);
  const char *p=strstr(j,pat); if(!p){
    snprintf(pat,sizeof pat,"\"%s\" : \"",key); p=strstr(j,pat); if(!p) return 0; }
  p+=strlen(pat);
  const char *e=p; while(*e && *e!='"') e++;
  return b64dec(p,(int)(e-p),o,cap); }

/* ---- TCP helpers ---- */
static int tcp_listen_local(int port, const char *bindaddr){
  int s=socket(AF_INET,SOCK_STREAM,0); if(s<0) return -1;
  int one=1; setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
  struct sockaddr_in a; memset(&a,0,sizeof a);
  a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  if(bindaddr && *bindaddr && inet_pton(AF_INET,bindaddr,&a.sin_addr)!=1){ close(s); return -1; } a.sin_port=htons((uint16_t)port);
  if(bind(s,(struct sockaddr*)&a,sizeof a)<0){ close(s); return -1; }
  if(listen(s,2)<0){ close(s); return -1; }
  return s; }
static int tcp_connect_local(int port){
  int s=socket(AF_INET,SOCK_STREAM,0); if(s<0) return -1;
  struct sockaddr_in a; memset(&a,0,sizeof a);
  a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(INADDR_LOOPBACK); a.sin_port=htons((uint16_t)port);
  if(connect(s,(struct sockaddr*)&a,sizeof a)<0){ close(s); return -1; }
  int one=1; setsockopt(s,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);
  return s; }

/* ---- HTTP GET/PUT on existing or new socket to port ---- */
static int http_verb(int port, const char *verb, const char *path, buf_t *out);
static int http_get(int port, const char *path, buf_t *out){
  return http_verb(port,"GET",path,out);
}
static int http_verb(int port, const char *verb, const char *path, buf_t *out){
  int s=tcp_connect_local(port); if(s<0) return -1;
  char req[512];
  snprintf(req,sizeof req,"%s %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",verb,path,port);
  if(wr_all(s,req,strlen(req))){ close(s); return -1; }
  out->n=0; buf_need(out,8192);
  int t0=(int)time(NULL), clen=-1, hlen=-1;
  for(;;){
    if((int)time(NULL)-t0>5) break;
    if(hlen>=0 && clen>=0 && out->n-hlen>=clen) break;
    fd_set rf; FD_ZERO(&rf); FD_SET(s,&rf);
    struct timeval tv={0,200000};
    if(select(s+1,&rf,NULL,NULL,&tv)<=0) continue;
    buf_need(out,out->n+8192);
    ssize_t k=read(s,out->p+out->n,8191);
    if(k<0){ if(errno==EINTR) continue; break; }
    if(!k) break;
    out->n+=(int)k; out->p[out->n]=0;
    if(hlen<0){
      char *he=strstr(out->p,"\r\n\r\n");
      if(he){
        hlen=(int)(he+4-out->p);
        const char *cl=strcasestr(out->p,"Content-Length:");
        if(cl && cl<he) clen=atoi(cl+15);
      }
    }
  }
  close(s);
  if(out->n<=0) return -1;
  char *body=strstr(out->p,"\r\n\r\n"); if(!body) return -1;
  body+=4; memmove(out->p,body,(size_t)(out->n-(body-out->p))+1); out->n=(int)strlen(out->p);
  return out->n>0?0:-1; }

/* ---- WebSocket client (text frames, masked) ---- */
static uint32_t rng32(void){ static uint32_t x=0xC0FFEE; x=x*1664525u+1013904223u; return x; }
static void b64_16(const unsigned char *in, char *out){
  static const char T[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int i,o=0;
  for(i=0;i<16;i+=3){
    unsigned n=((unsigned)in[i]<<16)|((unsigned)(i+1<16?in[i+1]:0)<<8)|(i+2<16?in[i+2]:0);
    out[o++]=T[(n>>18)&63]; out[o++]=T[(n>>12)&63];
    out[o++]=(i+1<16)?T[(n>>6)&63]:'='; out[o++]=(i+2<16)?T[n&63]:'='; }
  out[o]=0; }
static int ws_open(const char *url){
  /* ws://127.0.0.1:9222/devtools/page/UUID */
  if(strncmp(url,"ws://",5)) return -1;
  const char *host=url+5; const char *slash=strchr(host,'/'); if(!slash) return -1;
  char hbuf[128]; int n=(int)(slash-host); if(n<=0||n>=(int)sizeof hbuf) return -1;
  memcpy(hbuf,host,(size_t)n); hbuf[n]=0;
  int port=9222; char *col=strchr(hbuf,':'); if(col){ *col=0; port=atoi(col+1); }
  int s=tcp_connect_local(port); if(s<0) return -1;
  unsigned char key[16]; for(int i=0;i<16;i++) key[i]=(unsigned char)rng32();
  char k64[32]; b64_16(key,k64);
  char req[1024];
  snprintf(req,sizeof req,
    "GET %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
    "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n", slash, port, k64);
  if(wr_all(s,req,strlen(req))){ close(s); return -1; }
  char rsp[2048]; int got=0;
  while(got<(int)sizeof rsp-1){
    ssize_t k=read(s,rsp+got,(size_t)(sizeof rsp-1-got));
    if(k<0){ if(errno==EINTR) continue; close(s); return -1; }
    if(!k) break; got+=(int)k; rsp[got]=0;
    if(strstr(rsp,"\r\n\r\n")) break; }
  if(!strstr(rsp,"101")){ fprintf(stderr,"ws handshake failed: %.120s\n",rsp); close(s); return -1; }
  return s; }
static int ws_send_text(int fd, const char *s){
  size_t n=strlen(s);
  unsigned char hdr[14]; int h=0;
  hdr[h++]=0x81;
  unsigned char mask[4]; uint32_t r=rng32(); memcpy(mask,&r,4);
  if(n<126){ hdr[h++]=(unsigned char)(0x80|n); }
  else if(n<65536){ hdr[h++]=0x80|126; hdr[h++]=(unsigned char)(n>>8); hdr[h++]=(unsigned char)n; }
  else {
    hdr[h++]=0x80|127;
    for(int i=7;i>=0;i--) hdr[h++]=(unsigned char)((n>>((unsigned)i*8))&255); }
  memcpy(hdr+h,mask,4); h+=4;
  if(wr_all(fd,hdr,(size_t)h)) return -1;
  unsigned char *m=malloc(n?n:1); if(!m) return -1;
  for(size_t i=0;i<n;i++) m[i]=(unsigned char)s[i]^mask[i&3];
  int rc=wr_all(fd,m,n); free(m); return rc; }
static int ws_recv(int fd, buf_t *out){
  out->n=0;
  for(;;){
    unsigned char h[2];
    int r=rd_all(fd,h,2); if(r) return r;
    int fin=h[0]&0x80, op=h[0]&0x0f, masked=h[1]&0x80;
    uint64_t n=h[1]&0x7f;
    if(n==126){ unsigned char e[2]; if(rd_all(fd,e,2)) return -1; n=((uint64_t)e[0]<<8)|e[1]; }
    else if(n==127){ unsigned char e[8]; if(rd_all(fd,e,8)) return -1;
      n=0; for(int i=0;i<8;i++) n=(n<<8)|e[i]; }
    unsigned char mask[4]={0};
    if(masked && rd_all(fd,mask,4)) return -1;
    if((uint64_t)out->n+n>ASBC_MAX_MSG) return -1;
    buf_need(out,out->n+(int)n+1);
    if(n && rd_all(fd,out->p+out->n,(size_t)n)) return -1;
    if(masked) for(uint64_t i=0;i<n;i++) out->p[out->n+i]^=mask[i&3];
    out->n+=(int)n; out->p[out->n]=0;
    if(op==0x9){ unsigned char pong[2]={0x8A,0}; wr_all(fd,pong,2); out->n=0; return 0; }
    if(op==0x8) return -2;
    if(op==0xA){ out->n=0; return 0; }
    if(fin) return 1;
  }
}

/* ---- CDP ---- */
static int cdp(const char *fmt, ...){
  static char cmd[20000];
  va_list ap; va_start(ap,fmt); vsnprintf(cmd,sizeof cmd,fmt,ap); va_end(ap);
  if(g_ws<0) return -1;
  if(g_bidi){ bidi_from_cdp(cmd); return 0; } /* Firefox: same command as WebDriver BiDi */
  return ws_send_text(g_ws,cmd); }
static int cdp_id(void){ return g_cdp_id++; }
static void cdp_enable(void){
  int i;
  i=cdp_id(); cdp("{\"id\":%d,\"method\":\"Page.enable\"}",i);
  /* no Runtime.enable (0.78): pages can detect it (the console/stack-getter trick) and anti-bot checks such as Google's
     sign-in may count it as automation. Nothing here needs Runtime events: Runtime.evaluate works without it, and the
     password store no longer uses a Runtime binding (see pw_probe in features.inc). */
  i=cdp_id(); cdp("{\"id\":%d,\"method\":\"Network.enable\"}",i);
  i=cdp_id(); cdp("{\"id\":%d,\"method\":\"Browser.setDownloadBehavior\",\"params\":{\"behavior\":\"allow\",\"downloadPath\":\"%s\",\"eventsEnabled\":true}}",i,g_dlpath[0]?g_dlpath:"/tmp");
  /* navigator.webdriver: --disable-blink-features=AutomationControlled already makes it a plain false. The old JS
     override (own getter returning undefined) was itself a give-away, so it is gone. */
  feat_enable();
}
static void cdp_viewport(int w, int h){
  if(w<200) w=200; if(h<200) h=200; if(w>3840) w=3840; if(h>2160) h=2160;
  g_vw=w; g_vh=h; fprintf(stderr,"asb-chromed: viewport %dx%d\n",w,h);
  /* page zoom: smaller CSS viewport rendered at a higher device scale, so frames stay w x h device pixels */
  int z=g_zoom>0?g_zoom:100;
  int i=cdp_id();
  cdp("{\"id\":%d,\"method\":\"Emulation.setDeviceMetricsOverride\",\"params\":{\"width\":%d,\"height\":%d,\"deviceScaleFactor\":%d.%02d,\"mobile\":false,\"screenWidth\":%d,\"screenHeight\":%d,\"positionX\":0,\"positionY\":0}}",i,w*100/z,h*100/z,z/100,z%100,w*100/z,h*100/z);
  /* the emulated screen = the client's view, so element fullscreen (video maximise) fills the Amiga view, not the
     host's real/Xvfb screen; the browser window is also sized to it so fullscreen does not re-layout to another size */
}
static long long g_cast_due;
static void cdp_screencast(int on){
  if(g_bidi){ g_screencast_on=on; if(on){ free(g_prev); g_prev=NULL; g_cast_due=0; } return; } /* frames are polled: bidi_cast_tick */
  int i=cdp_id();
  if(on){
    free(g_prev); g_prev=NULL; /* next frame goes out whole (new client, new tab or restarted cast) */
    cdp("{\"id\":%d,\"method\":\"Page.startScreencast\",\"params\":{\"format\":\"png\",\"maxWidth\":%d,\"maxHeight\":%d,\"everyNthFrame\":2}}",i,g_vw>0?g_vw:1280,g_vh>0?g_vh:720);
    g_screencast_on=1; }
  else {
    cdp("{\"id\":%d,\"method\":\"Page.stopScreencast\"}",i);
    g_screencast_on=0; }
}
static void cdp_nav(const char *url){
  g_loading=1; snprintf(g_url,sizeof g_url,"%s",url);
  int i=cdp_id();
  /* JSON-escape quotes in url */
  char esc[2400]; int o=0;
  for(const char *p=url;*p&&o<(int)sizeof esc-2;p++){
    if(*p=='\\'||*p=='"'){ esc[o++]='\\'; esc[o++]=*p; }
    else esc[o++]=*p; }
  esc[o]=0;
  cdp("{\"id\":%d,\"method\":\"Page.navigate\",\"params\":{\"url\":\"%s\"}}",i,esc);
}
static void send_meta(void){
  if(g_cli<0) return;
  unsigned char b[2800]; int o=1;
  b[0]=(unsigned char)((1/*back*/)|(2/*fwd*/)|(g_loading?4:0)); /* Chrome owns history */
  hist_note();
  char du[2400]; display_url(g_url,du,sizeof du);
  unsigned ul=(unsigned)strlen(du), tl=(unsigned)strlen(g_title);
  if(ul>2400) ul=2400; if(tl>250) tl=250;
  asbc_put_u16(b+o,ul); o+=2; memcpy(b+o,du,ul); o+=(int)ul;
  asbc_put_u16(b+o,tl); o+=2; memcpy(b+o,g_title,tl); o+=(int)tl;
  asbc_send(g_cli,ASBC_META,b,(uint32_t)o);
}
static void send_status(const char *s){
  if(g_cli<0) return;
  asbc_send(g_cli,ASBC_STATUS,s,(uint32_t)strlen(s));
}
static void send_tabs(void){
  if(g_cli<0) return;
  unsigned char b[2000]; int o=0, i;
  b[o++]=(unsigned char)g_ntab;
  b[o++]=(unsigned char)g_tab;
  for(i=0;i<g_ntab && o<(int)sizeof b-80;i++){
    const char *t=g_tabs[i].title[0]?g_tabs[i].title:g_tabs[i].url;
    unsigned n=(unsigned)strlen(t); if(n>60) n=60;
    asbc_put_u16(b+o,n); o+=2; memcpy(b+o,t,n); o+=(int)n;
  }
  asbc_send(g_cli,ASBC_TABS,b,(uint32_t)o);
}
static int g_ntab_prev; static char g_cur_prev[80];
static int load_tabs(void){
  buf_t http={0};
  g_ntab_prev=g_ntab; if(g_tab>=0 && g_tab<g_ntab) scopy(g_cur_prev,sizeof g_cur_prev,g_tabs[g_tab].id); else g_cur_prev[0]=0;
  if(g_bidi){ if(bidi_tree()) return -1; goto order; }
  if(http_get(g_dbg,"/json/list",&http)){ free(http.p); return -1; }
  g_ntab=0;
  /* /json/list is an array of flat objects: look at each {...} on its own so fields never leak between tabs */
  char *p=http.p?http.p:"";
  while(g_ntab<MAXTAB && (p=strchr(p,'{'))){
    char *e=strchr(p,'}'); if(!e) break;
    *e=0;
    int ispage=strstr(p,"\"type\": \"page\"")||strstr(p,"\"type\":\"page\"");
    int bad=strstr(p,"chrome-untrusted:")||strstr(p,"chrome-extension:")||strstr(p,"devtools://");
    if(ispage && !bad){
      json_str(p,"webSocketDebuggerUrl",g_tabs[g_ntab].ws,sizeof g_tabs[0].ws);
      json_str(p,"id",g_tabs[g_ntab].id,sizeof g_tabs[0].id);
      json_str(p,"title",g_tabs[g_ntab].title,sizeof g_tabs[0].title);
      json_str(p,"url",g_tabs[g_ntab].url,sizeof g_tabs[0].url);
      if(g_tabs[g_ntab].ws[0] && strstr(g_tabs[g_ntab].ws,"/devtools/page/")) g_ntab++;
    }
    p=e+1;
  }
  order:
  /* Chrome lists tabs most-recently-used first; keep the order the client already shows (new tabs go last) */
  { char (*order)[80]=g_order; int norder=g_norder; char cur[80]="";
    if(g_tab>=0 && g_tab<g_ntab_prev) scopy(cur,sizeof cur,g_cur_prev);
    int used[MAXTAB]={0}, k=0; __typeof__(g_tabs[0]) tmp[MAXTAB];
    for(int i=0;i<norder;i++) for(int j=0;j<g_ntab;j++) if(!used[j]&&!strcmp(order[i],g_tabs[j].id)){ tmp[k++]=g_tabs[j]; used[j]=1; break; }
    for(int j=g_ntab-1;j>=0;j--) if(!used[j]) tmp[k++]=g_tabs[j];
    for(int i=0;i<k;i++){ g_tabs[i]=tmp[i]; scopy(order[i],sizeof order[0],tmp[i].id); }
    g_norder=k; (void)norder;
    if(cur[0]){ for(int i=0;i<k;i++) if(!strcmp(order[i],cur)){ g_tab=i; break; } } }
  if(g_tab>=g_ntab) g_tab=g_ntab?g_ntab-1:0;
  free(http.p);
  return g_ntab;
}
static int tab_by_id(const char *id){
  for(int i=0;i<g_ntab;i++) if(id[0] && !strcmp(g_tabs[i].id,id)) return i;
  return -1; }
static void kick_arm(void);
static int attach_tab(int idx){
  if(idx<0||idx>=g_ntab||(!g_bidi && !g_tabs[idx].ws[0])) return -1;
  if(g_screencast_on) cdp_screencast(0);
  if(g_bidi) scopy(g_ctx,sizeof g_ctx,g_tabs[idx].id); /* one BiDi connection serves every tab */
  else {
  if(g_ws>=0){ close(g_ws); g_ws=-1; }
  g_ws=ws_open(g_tabs[idx].ws);
  if(g_ws<0){ fprintf(stderr,"asb-chromed: attach ws failed %s\n",g_tabs[idx].ws); return -1; } }
  g_tab=idx;
  free(g_prev); g_prev=NULL; g_prev_w=g_prev_h=0;
  cdp_enable();
  /* 0.85: a window.open() popup of a page we are attached to starts paused, waiting for its debugger ("Debugger
     paused in another tab" over the opener): let it run, or it stays a blank tab for good */
  { int i=cdp_id(); cdp("{\"id\":%d,\"method\":\"Runtime.runIfWaitingForDebugger\"}",i); }
  { int i=cdp_id(); cdp("{\"id\":%d,\"method\":\"Page.bringToFront\"}",i); } /* background tabs send no screencast frames */
  cdp_viewport(g_vw,g_vh);
  cdp_screencast(1);
  /* 0.85: a tab that was in the background and does not change sends no screencast frame, so the client kept the
     other tab's picture: if no frame comes within 600 ms, send one screenshot (as after a viewport change) */
  kick_arm();
  snprintf(g_url,sizeof g_url,"%s",g_tabs[idx].url);
  snprintf(g_title,sizeof g_title,"%s",g_tabs[idx].title[0]?g_tabs[idx].title:"Tab");
  send_meta(); send_tabs();
  return 0;
}

static int g_avdelay_ms; /* 0.81: picture hold-back for A/V sync (cmd avdelay, see vq_pump) */
#include "features.inc"
static unsigned char g_pkt[ASBC_MAX_MSG];
static unsigned char g_cap[ASBC_MAX_MSG];
static long g_sent_by_q; static int g_inflight; /* FRAME/SCROLL messages not yet acknowledged */
/* 0.81 A/V sync: when each in-flight update was captured by Chrome (wall ms); the ACK says it is on the Amiga screen,
   so capture->ACK is the video latency the client's audio buffer is matched to (sent in ASBC_STATS) */
#define AU_HOST_MS 70   /* Chrome output + pulse + parec (20 ms) + half a 40 ms packet, measured roughly */
static long long g_cap_ms, g_fq[64]; static int g_fq_r, g_fq_w; static int g_vlat_ms;
static long long wall_ms(void){ struct timeval t; gettimeofday(&t,NULL); return (long long)t.tv_sec*1000+t.tv_usec/1000; }
static void inflight_push(void){ g_inflight++; if(g_fq_w-g_fq_r<64) g_fq[(g_fq_w++)&63]=g_cap_ms; }
static void inflight_reset(void){ g_inflight=0; g_fq_r=g_fq_w=0; }
static int send_qoi_tile(int cssw,int cssh,int x,int y,int tw,int th,const unsigned char *rgb,int stride){
  unsigned char *tile=(unsigned char*)malloc((size_t)tw*(size_t)th*3);
  if(!tile) return -1;
  int row;
  for(row=0;row<th;row++) memcpy(tile+(size_t)row*tw*3, rgb+(size_t)(y+row)*stride+(size_t)x*3, (size_t)tw*3);
  qoi_desc d; d.width=(unsigned)tw; d.height=(unsigned)th; d.channels=3; d.colorspace=QOI_SRGB;
  int qn=0; void *q=qoi_encode(tile,&d,&qn); free(tile);
  if(!q||qn<1||qn+16>(int)sizeof g_pkt||qn>1200000){ fprintf(stderr,"asb-chromed: qoi skip %d %dx%d\n",qn,tw,th); free(q); return -1; }
  unsigned char *p=g_pkt;
  asbc_put_u16(p,(unsigned)cssw); asbc_put_u16(p+2,(unsigned)cssh);
  asbc_put_u16(p+4,(unsigned)x); asbc_put_u16(p+6,(unsigned)y);
  asbc_put_u16(p+8,(unsigned)tw); asbc_put_u16(p+10,(unsigned)th);
  p[12]=2; p[13]=0;
  memcpy(p+14,q,(size_t)qn); free(q);
  fprintf(stderr,"asb-chromed: qoi %d bytes tile %dx%d @%d,%d of %dx%d\n",qn,tw,th,x,y,cssw,cssh);
  asbc_send(g_cli,ASBC_FRAME,g_pkt,(uint32_t)(14+qn)); g_sent_by_q+=14+qn; inflight_push();
  return 0;
}
#include "frames.inc"
static void send_png_full(int cssw,int cssh,const unsigned char *png,int n){
  if(14+n>=(int)sizeof g_pkt) return;
  asbc_put_u16(g_pkt,(unsigned)cssw); asbc_put_u16(g_pkt+2,(unsigned)cssh);
  asbc_put_u16(g_pkt+4,0); asbc_put_u16(g_pkt+6,0);
  asbc_put_u16(g_pkt+8,(unsigned)cssw); asbc_put_u16(g_pkt+10,(unsigned)cssh);
  g_pkt[12]=1; g_pkt[13]=0;
  memcpy(g_pkt+14,png,(size_t)n);
  fprintf(stderr,"asb-chromed: png full %d bytes %dx%d\n",n,cssw,cssh);
  asbc_send(g_cli,ASBC_FRAME,g_pkt,(uint32_t)(14+n)); inflight_push();
}
static int g_clip_id;
static int g_skipped;
static int g_frames_seen, g_shot_id; static long long g_shot_due;
static long long now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (long long)t.tv_sec*1000+t.tv_nsec/1000000; }
/* decode one PNG in g_cap (screencast frame or captureScreenshot) and send the changed tile */
/* scroll detection: find dy so that most rows of the new frame equal old rows shifted by dy (row hashes) */
static unsigned long long *g_hn, *g_ho; static int g_hcap;
static unsigned long long row_hash(const unsigned char *r,int n){ unsigned long long h=1469598103934665603ULL; for(int i=0;i<n;i+=3){ h^=(unsigned long long)(r[i]|r[i+1]<<8|r[i+2]<<16); h*=1099511628211ULL; } return h; }
static int detect_scroll(const unsigned char *rgb,int iw,int ih,int *y0,int *y1){
  if(ih>g_hcap){ free(g_hn); free(g_ho); g_hn=malloc(sizeof *g_hn*ih); g_ho=malloc(sizeof *g_ho*ih); g_hcap=(g_hn&&g_ho)?ih:0; if(!g_hcap) return 0; }
  int st=iw*3, same=0;
  for(int r=0;r<ih;r++){ g_hn[r]=row_hash(rgb+(long)r*st,st); g_ho[r]=row_hash(g_prev+(long)r*st,st); if(g_hn[r]==g_ho[r]) same++; }
  if(same>ih*7/8) return 0;
  int cand[24], nc=0;
  for(int r=1;r<ih-1 && nc<24;r+=ih/40+1){ if(g_hn[r]==g_ho[r] || g_hn[r]==g_hn[r-1]) continue;   /* changed, distinctive row */
    for(int q=0;q<ih;q++) if(g_ho[q]==g_hn[r] && q!=r){ int d=r-q, k; for(k=0;k<nc;k++) if(cand[k]==d) break; if(k==nc && nc<24) cand[nc++]=d; break; } }
  int best=0, bm=0, b0=0, b1=0;
  for(int k=0;k<nc;k++){ int d=cand[k], m=0, lo=-1, hi=-1;
    for(int r=0;r<ih;r++){ int q=r-d; if(q<0||q>=ih) continue; if(g_hn[r]==g_ho[q] && g_hn[r]!=g_ho[r]){ m++; if(lo<0) lo=r; hi=r; } }
    if(m>bm){ bm=m; best=d; b0=lo; b1=hi; } }
  if(!best || bm<ih/4) return 0;
  *y0=b0; *y1=b1; return best; }

static unsigned char *g_pend; static int g_pend_w, g_pend_h; static long long g_pend_cap;
static void send_update(unsigned char *rgb,int iw,int ih,int n){
      int stride=iw*3;
      if(!g_prev || g_prev_w!=iw || g_prev_h!=ih){
        if(send_tile(iw,ih,0,0,iw,ih,rgb,stride)!=0 && n>0) send_png_full(iw,ih,g_cap,n);
      } else {
        int sy0, sy1, dy=(g_ff&ASBC_FF_SCROLL)?detect_scroll(rgb,iw,ih,&sy0,&sy1):0;
        if(dy){ /* client moves rows [sy0..sy1] from old row r-dy; we do the same to g_prev, then only the rest differs */
          unsigned char b[10]; asbc_put_u16(b,0); asbc_put_u16(b+2,(unsigned)sy0); asbc_put_u16(b+4,(unsigned)iw); asbc_put_u16(b+6,(unsigned)(sy1-sy0+1)); asbc_put_i16(b+8,dy);
          if(asbc_send(g_cli,ASBC_SCROLL,b,10)==0){ inflight_push(); g_sent_by+=10;
            memmove(g_prev+(long)sy0*stride,g_prev+(long)(sy0-dy)*stride,(size_t)(sy1-sy0+1)*stride);
            if(getenv("ASB_DEBUG")) fprintf(stderr,"asb-chromed: scroll rows %d..%d by %d\n",sy0,sy1,dy); } }
        int minx=iw, miny=ih, maxx=-1, maxy=-1, y, x;
        for(y=0;y<ih;y++){
          const unsigned char *a=rgb+y*stride, *b=g_prev+y*stride;
          if(!memcmp(a,b,(size_t)stride)) continue;
          for(x=0;x<iw;x++){
            if(a[0]!=b[0]||a[1]!=b[1]||a[2]!=b[2]){
              if(x<minx) minx=x; if(x>maxx) maxx=x;
              if(y<miny) miny=y; if(y>maxy) maxy=y;
            }
            a+=3; b+=3;
          }
        }
        if(maxx>=0){
          minx=minx>2?minx-2:0; miny=miny>2?miny-2:0;
          maxx=maxx+2<iw?maxx+2:iw-1; maxy=maxy+2<ih?maxy+2:ih-1;
          int tw=maxx-minx+1, th=maxy-miny+1;
          long area=(long)tw*th, full=(long)iw*ih;
          if(area*2>full){
            if(send_tile(iw,ih,0,0,iw,ih,rgb,stride)!=0 && n>0) send_png_full(iw,ih,g_cap,n);
          } else send_tile(iw,ih,minx,miny,tw,th,rgb,stride);
        }
      }
      free(g_prev); g_prev=rgb; g_prev_w=iw; g_prev_h=ih;
}
/* flow control: with ASBC_FF_ACK at most 2 updates are in flight; newer frames replace the waiting one (frame skip) */
static void frame_png(int n){
  if(n<=0 || g_cli<0) return;
  int iw,ih,comp;
  unsigned char *rgb=stbi_load_from_memory(g_cap,n,&iw,&ih,&comp,3);
  if(!rgb || iw<1 || ih<1){ free(rgb); return; }
  if((g_ff&ASBC_FF_ACK) && g_inflight>=2){ free(g_pend); g_pend=rgb; g_pend_w=iw; g_pend_h=ih; g_pend_cap=g_cap_ms; g_skipped++; return; }
  send_update(rgb,iw,ih,n);
}
static void on_ack(void){
  if(g_inflight>0) g_inflight--;
  if(g_fq_r<g_fq_w){ long long c=g_fq[(g_fq_r++)&63]; long long l=wall_ms()-c;
    if(c>0 && l>=0 && l<10000) g_vlat_ms=g_vlat_ms?(int)((g_vlat_ms*7+l)/8):(int)l; }
  if(g_pend && g_inflight<2 && g_cli>=0){ unsigned char *r=g_pend; g_pend=NULL; long long sc=g_cap_ms; g_cap_ms=g_pend_cap; send_update(r,g_pend_w,g_pend_h,0); g_cap_ms=sc; }
}
/* 0.81 A/V sync, the other half: on a fast link the picture is on the Amiga screen long before the sound comes out
   of its buffers. While sound plays the client asks ("cmd avdelay MS") for the picture to be held back that long;
   frames wait here (decoding and diffing happen when they are released). 0 = off. */
#define VQ_MAX 48
static struct { unsigned char *d; int n; long long cap; } g_vq[VQ_MAX]; static int g_vq_n;
static void vq_clear(void){ for(int i=0;i<g_vq_n;i++) free(g_vq[i].d); g_vq_n=0; }
static void vq_put(const unsigned char *d,int n,long long cap){
  if(g_vq_n==VQ_MAX){ free(g_vq[0].d); memmove(g_vq,g_vq+1,sizeof g_vq[0]*(VQ_MAX-1)); g_vq_n--; }
  unsigned char *c=malloc((size_t)n); if(!c) return; memcpy(c,d,(size_t)n);
  g_vq[g_vq_n].d=c; g_vq[g_vq_n].n=n; g_vq[g_vq_n].cap=cap; g_vq_n++; }
static void vq_pump(void){
  long long now=wall_ms();
  while(g_vq_n && (g_vq[0].cap+g_avdelay_ms<=now || g_cli<0)){
    int n=g_vq[0].n; if(n<=(int)sizeof g_cap) memcpy(g_cap,g_vq[0].d,(size_t)n); else n=0;
    g_cap_ms=g_vq[0].cap+g_avdelay_ms;   /* the measured video latency stays the pipeline's own */
    free(g_vq[0].d); memmove(g_vq,g_vq+1,sizeof g_vq[0]*(size_t)(g_vq_n-1)); g_vq_n--;
    if(n>0 && g_cli>=0){ long long t0=us_now(); int qoi=g_pf==ASBC_PF_QOI; g_sent_by_q=0; frame_png(n); g_enc_us+=us_now()-t0; g_enc_n++; if(qoi) g_sent_by+=g_sent_by_q; g_sent_fr++; } } }
/* after a viewport change Chrome may send no screencast frame until the page itself changes (a resized
   Workbench window kept showing the old layout): if none came, grab one screenshot and send it as a frame */
static long long g_kick_due; static long g_kick_seen; static int g_kick_id;
static void kick_arm(void){ if(!g_bidi){ g_kick_due=us_now()+600000; g_kick_seen=g_frames_seen; } }
static void kick_tick(void){
  if(!g_kick_due || us_now()<g_kick_due) return;
  g_kick_due=0; if(g_bidi || g_cli<0 || g_frames_seen!=g_kick_seen) return;
  g_kick_id=cdp_id(); cdp("{\"id\":%d,\"method\":\"Page.captureScreenshot\",\"params\":{\"format\":\"png\"}}",g_kick_id); }
static void on_cdp(const char *msg){
  char method[80]; if(!json_str(msg,"method",method,sizeof method)) method[0]=0;
  if(!method[0] && g_kick_id && json_int(msg,"id",-1)==g_kick_id){ g_kick_id=0;
    int n=json_b64_field(msg,"data",g_cap,(int)sizeof g_cap); if(n>0 && g_frames_seen==g_kick_seen){ free(g_prev); g_prev=NULL; frame_png(n); } return; }
  if(!method[0] && feat_reply(msg)) return;
  if(method[0] && feat_event(method,msg)) return;
  if(!strcmp(method,"Page.screencastFrame")){
    int sid=json_int(msg,"sessionId",0);
    int n=json_b64_field(msg,"data",g_cap,(int)sizeof g_cap);
    if(getenv("ASB_DEBUG")) fprintf(stderr,"asb-chromed: screencast frame %d bytes (msg %d)\n",n,(int)strlen(msg));
    if(n>0) g_frames_seen++;
    /* element fullscreen (video maximise) and leaving it make Chrome drop our metrics override, so frames come at the
       host screen's size/aspect: put the client's viewport back (rate-limited) */
    int skip=0;
    if(n>24 && !memcmp(g_cap+12,"IHDR",4) && g_vw>0 && g_vh>0 && !getenv("ASB_NO_VIEWFIX")){
      int pw=(g_cap[16]<<24)|(g_cap[17]<<16)|(g_cap[18]<<8)|g_cap[19], ph=(g_cap[20]<<24)|(g_cap[21]<<16)|(g_cap[22]<<8)|g_cap[23];
      static long long fixat;
      if((pw!=g_vw||ph!=g_vh) && now_ms()-fixat>400){ fixat=now_ms(); fprintf(stderr,"asb-chromed: frame %dx%d != view %dx%d, re-applying viewport\n",pw,ph,g_vw,g_vh); cdp_viewport(g_vw,g_vh); }
      /* hold back wrong-size frames while the fix lands (at most 2 s, then show whatever Chrome gives) */
      static long long mis; if(pw!=g_vw||ph!=g_vh){ if(!mis) mis=now_ms(); skip=now_ms()-mis<2000; } else mis=0; }
    if(skip){ int i=cdp_id(); cdp("{\"id\":%d,\"method\":\"Page.screencastFrameAck\",\"params\":{\"sessionId\":%d}}",i,sid); return; }
    { /* metadata.timestamp: when Chrome captured the frame (s since the epoch); else now */
      const char *ts=strstr(msg,"\"timestamp\":"); double tsec=ts?strtod(ts+12,NULL):0; long long now=wall_ms();
      g_cap_ms=(tsec>1e9 && (long long)(tsec*1000)<=now+50 && now-(long long)(tsec*1000)<5000)?(long long)(tsec*1000):now; }
    if(g_avdelay_ms>0 && n>0){ vq_put(g_cap,n,g_cap_ms); vq_pump(); }
    else { long long t0=us_now(); int qoi=g_pf==ASBC_PF_QOI; g_sent_by_q=0; frame_png(n); g_enc_us+=us_now()-t0; g_enc_n++; if(qoi) g_sent_by+=g_sent_by_q; g_sent_fr++; }
    { int i=cdp_id();
      cdp("{\"id\":%d,\"method\":\"Page.screencastFrameAck\",\"params\":{\"sessionId\":%d}}",i,sid); }
    return; }
  if(!strcmp(method,"Page.frameNavigated")||!strcmp(method,"Page.navigatedWithinDocument")){
    /* only the main frame: iframes (ads, about:blank helpers) carry "parentId" and must not change the URL bar */
    if(strstr(msg,"\"parentId\"")) return;
    char u[2400]; if(json_str(msg,"url",u,sizeof u)) scopy(g_url,sizeof g_url,u);
    if(!strcmp(method,"Page.frameNavigated")) g_loading=1;
    send_meta();
    int i=cdp_id();
    cdp("{\"id\":%d,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":\"document.title\",\"returnByValue\":true}}",i);
    return; }
  if(!strcmp(method,"Browser.downloadWillBegin")){
    char fn[256]; if(!json_str(msg,"suggestedFilename",fn,sizeof fn)) json_str(msg,"url",fn,sizeof fn);
    char st[280]; snprintf(st,sizeof st,"Downloading %s",fn[0]?fn:"file"); send_status(st);
    return; }
  if(!strcmp(method,"Browser.downloadProgress")){
    char st[40]; json_str(msg,"state",st,sizeof st);
    if(!strcmp(st,"completed")) send_status("Download finished");
    else if(!strcmp(st,"canceled")) send_status("Download canceled");
    return; }
  if(!strcmp(method,"Page.loadEventFired")||!strcmp(method,"Page.domContentEventFired")){
    g_loading=0; send_meta();
    /* a cross-process navigation (e.g. view-source: -> http) can silently end the screencast: restart it once the page has loaded */
    if(!strcmp(method,"Page.loadEventFired") && g_screencast_on && g_cli>=0){ cdp_screencast(0); cdp_screencast(1); g_frames_seen=0; g_shot_due=now_ms()+700; }
    int i=cdp_id();
    cdp("{\"id\":%d,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":\"location.href+'\\\\n'+document.title\",\"returnByValue\":true}}",i);
    return; }
  if(g_shot_id && json_int(msg,"id",0)==g_shot_id){
    g_shot_id=0; int n=json_b64_field(msg,"data",g_cap,(int)sizeof g_cap);
    if(n>0 && !g_frames_seen){ if(getenv("ASB_DEBUG")) fprintf(stderr,"asb-chromed: no screencast frame after load, sent a screenshot\n"); frame_png(n); }
    return; }
  if(strstr(msg,"\"result\"") && strstr(msg,"\"value\"")){
    char v[8000];
    int rid=json_int(msg,"id",0);
    if(json_str(msg,"value",v,sizeof v)){
      if(g_clip_id && rid==g_clip_id){
        g_clip_id=0;
        if(g_cli>=0 && v[0]) asbc_send(g_cli,ASBC_CLIP,v,(uint32_t)strlen(v));
        return;
      }
      char *nl=strchr(v,'\n');
      if(nl){ char *rs=strrchr(nl+1,'\n');
        if(rs && (!strcmp(rs+1,"complete")||!strcmp(rs+1,"interactive")||!strcmp(rs+1,"loading"))){ /* from the 2 s poll */
          *rs=0; int ld=!strcmp(rs+1,"loading"); if(ld!=g_loading){ g_loading=ld; *nl=0; scopy(g_url,sizeof g_url,v); scopy(g_title,sizeof g_title,nl+1); send_meta(); goto tabsync; } }
        *nl=0; if(strcmp(g_url,v)||strcmp(g_title,nl+1)){ scopy(g_url,sizeof g_url,v); scopy(g_title,sizeof g_title,nl+1); send_meta(); } else return; }
      else if(v[0] && !strchr(v,':')){ scopy(g_title,sizeof g_title,v); send_meta(); }
      tabsync:
      if(g_tab>=0 && g_tab<g_ntab){
        scopy(g_tabs[g_tab].url,sizeof g_tabs[0].url,g_url);
        scopy(g_tabs[g_tab].title,sizeof g_tabs[0].title,g_title);
        send_tabs();
      }
    }
  }
}


/* ---- 0.85: browser-target connection: downloads and tabs Chrome opens or closes by itself ---- */
static void bcon_open(void){
  buf_t r={0}; char u[400]="";
  if(http_get(g_dbg,"/json/version",&r)==0 && r.p) json_str(r.p,"webSocketDebuggerUrl",u,sizeof u);
  free(r.p);
  if(!u[0] || (g_bcon=ws_open(u))<0){ g_bcon=-1; return; }
  char c[900];
  snprintf(c,sizeof c,"{\"id\":%d,\"method\":\"Target.setDiscoverTargets\",\"params\":{\"discover\":true}}",cdp_id()); ws_send_text(g_bcon,c);
  snprintf(c,sizeof c,"{\"id\":%d,\"method\":\"Browser.setDownloadBehavior\",\"params\":{\"behavior\":\"allow\",\"downloadPath\":\"%s\",\"eventsEnabled\":true}}",cdp_id(),g_dlpath[0]?g_dlpath:"/tmp");
  ws_send_text(g_bcon,c);
  fprintf(stderr,"asb-chromed: browser connection open (downloads to %s, tab events)\n",g_dlpath); }
/* the tab list changed in Chrome: reload it; a tab Chrome opened (not ours from tab_new) is in front in Chrome,
   so show it; if the shown tab went away (window.close, popup done), show its opener or a neighbour */
static char g_auto_id[80], g_auto_opener[80];
static void tabs_resync(void){
  char cur[80]=""; char old[MAXTAB][80]; int nold=g_ntab;
  if(g_tab>=0 && g_tab<g_ntab) scopy(cur,sizeof cur,g_tabs[g_tab].id);
  for(int i=0;i<g_ntab;i++) scopy(old[i],sizeof old[0],g_tabs[i].id);
  if(load_tabs()<0) return;
  int nw=-1;
  for(int j=0;j<g_ntab;j++){ int seen=0; for(int i=0;i<nold;i++) if(!strcmp(old[i],g_tabs[j].id)) seen=1;
    if(!seen && strcmp(g_tabs[j].id,g_tab_mine)) nw=j; }
  if(nw>=0 && g_cli>=0){
    fprintf(stderr,"asb-chromed: tab opened by the page: %.120s\n",g_tabs[nw].url);
    scopy(g_auto_id,sizeof g_auto_id,g_tabs[nw].id); scopy(g_auto_opener,sizeof g_auto_opener,cur);
    attach_tab(nw); send_status("Opened in a new tab"); return; }
  if(cur[0] && tab_by_id(cur)<0 && g_ntab>0){
    int k=-1; if(!strcmp(cur,g_auto_id)) k=tab_by_id(g_auto_opener);
    if(k<0) k=g_tab<g_ntab?g_tab:g_ntab-1;
    fprintf(stderr,"asb-chromed: shown tab closed by the page, showing tab %d\n",k);
    attach_tab(k); return; }
  { int k=tab_by_id(cur); if(k>=0) g_tab=k; }
  send_tabs(); }
static void on_bcon(const char *msg){
  char method[80]; if(!json_str(msg,"method",method,sizeof method)) return;
  if(!strncmp(method,"Browser.download",16)){ feat_event(method,msg); return; }
  if(!strcmp(method,"Target.targetCreated")||!strcmp(method,"Target.targetDestroyed")||!strcmp(method,"Target.targetInfoChanged")){
    if(strstr(msg,"\"type\":\"page\"")||!strcmp(method,"Target.targetDestroyed")){
      long long d=now_ms()+(strcmp(method,"Target.targetInfoChanged")?250:1000);
      if(!g_tabs_due || d<g_tabs_due) g_tabs_due=d; } } }
static void bcon_tick(void){
  if(g_bidi || !g_chrome) return;
  long long t=now_ms();
  if(g_bcon<0 && t-g_bcon_try>3000){ g_bcon_try=t; bcon_open(); }
  /* no browser connection (old Chrome): look at the tab list every 2 s instead */
  if(g_bcon<0 && g_cli>=0 && !g_tabs_due){ static long long lastpoll; if(t-lastpoll>2000){ lastpoll=t; g_tabs_due=t; } }
  if(g_tabs_due && t>=g_tabs_due && g_cli>=0){ g_tabs_due=0; tabs_resync(); } }
#include "bidi.inc"
#include "audio.inc"

static int g_mx, g_my;
static int browser_switch(int want);
static void on_client(int op, unsigned char *p, int n){
  if(op!=ASBC_MOUSE) fprintf(stderr,"asb-chromed: op %d n=%d\n",op,n);
  switch(op){
  case ASBC_HELLO:
    if(n>=5 && p[4] && p[4]!=(g_bidi?2:1)){ g_vw=asbc_get_u16(p); g_vh=asbc_get_u16(p+2); browser_switch(p[4]); } /* BROWSER= from the client */
    g_au_caps=n>=6?p[5]:0; if(g_au_caps) audio_start_cap(); else audio_stop_cap();
    if(n>=4){ cdp_viewport(asbc_get_u16(p), asbc_get_u16(p+2)); cdp_screencast(1); send_meta(); load_tabs(); send_tabs(); send_zoom(); kick_arm(); }
    break;
  case ASBC_ACK: on_ack(); break;
  case ASBC_AUDIOCTL: if(n>=1){ g_au_on=p[0]!=0; if(g_au_on) audio_start_cap(); else audio_stop_cap(); } break;
  case ASBC_FMT: on_fmt(p,n); inflight_reset(); free(g_pend); g_pend=NULL; if(g_screencast_on){ cdp_screencast(0); cdp_screencast(1); } kick_arm(); break;
  case ASBC_RESIZE:
    if(n>=4){ cdp_viewport(asbc_get_u16(p), asbc_get_u16(p+2)); kick_arm(); }
    break;
  case ASBC_NAV:{
    char u[2400]; int m=n<(int)sizeof u-1?n:(int)sizeof u-1; memcpy(u,p,(size_t)m); u[m]=0;
    if(!u[0]) break;
    { char r[2600]; if(nav_rewrite(u,r,sizeof r)) scopy(u,sizeof u,r); }
    cdp_nav(u); send_meta();
    break; }
  /* CDP has no Page.goBack/goForward; history.back() works for both full loads and SPA history */
  case ASBC_BACK:{ int i=cdp_id(); cdp("{\"id\":%d,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":\"history.back()\"}}",i); break; }
  case ASBC_FWD:{ int i=cdp_id(); cdp("{\"id\":%d,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":\"history.forward()\"}}",i); break; }
  case ASBC_RELOAD:{ int i=cdp_id(); cdp("{\"id\":%d,\"method\":\"Page.reload\"}",i); g_loading=1; send_meta(); break; }
  case ASBC_STOP:{ int i=cdp_id(); cdp("{\"id\":%d,\"method\":\"Page.stopLoading\"}",i); g_loading=0; send_meta(); break; }
  case ASBC_FIND:{
    if(n>0){ int m=n<(int)sizeof g_findq-1?n:(int)sizeof g_findq-1; char q[400]; memcpy(q,p,(size_t)m); q[m]=0;
      if(strcmp(q,g_findq)){ scopy(g_findq,sizeof g_findq,q); find_run(1,1); break; } }
    find_run(1,0);
    break; }
  case ASBC_CMD:{
    char c[2600]; int m=n<(int)sizeof c-1?n:(int)sizeof c-1; memcpy(c,p,(size_t)m); c[m]=0;
    fprintf(stderr,"asb-chromed: cmd %.80s\n",c);
    do_cmd(c);
    break; }
  case ASBC_TABNEW: tab_new(NULL); break;
  case ASBC_TABSEL:{
    int idx=n?p[0]:0;
    load_tabs();
    attach_tab(idx);
    break; }
  case ASBC_TABCLS:{
    int idx=n?p[0]:g_tab;
    load_tabs();
    if(idx>=0 && idx<g_ntab && g_tabs[idx].id[0]){
      closed_push(idx==g_tab?g_url:g_tabs[idx].url);
      if(g_bidi) bidi_tab_close(g_tabs[idx].id);
      else { char path[160]; snprintf(path,sizeof path,"/json/close/%s",g_tabs[idx].id);
      buf_t r={0}; http_get(g_dbg,path,&r); free(r.p); }
    }
    usleep(100000);
    load_tabs();
    if(g_ntab<1){ send_tabs(); break; }
    if(g_tab>=g_ntab) g_tab=g_ntab-1;
    attach_tab(g_tab);
    break; }
  case ASBC_COPY:{
    /* payload byte (optional, 0.73+): 0 copy, 1 cut, 2 select all. Copy/cut read a focused input's own selection. */
    int mode=n>0?p[0]:0; if(mode>2) mode=0;
    g_clip_id=cdp_id();
    cdp("{\"id\":%d,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":\"(function(m){try{var e=document.activeElement,s='',f=e&&/^(INPUT|TEXTAREA)$/.test(e.tagName)&&e.selectionStart!=null;"
      "if(m==2){if(f)e.select();else document.execCommand('selectAll');return ''}"
      "s=f?e.value.substring(e.selectionStart,e.selectionEnd):window.getSelection().toString();"
      "if(m==1&&s)document.execCommand('delete');return s}catch(x){return ''}})(%d)\",\"returnByValue\":true}}",g_clip_id,mode);
    break; }
  case ASBC_PASTE:{
    char esc[8000]; int o=0, m=n;
    if(m>(int)sizeof esc/2-4) m=(int)sizeof esc/2-4;
    for(int i=0;i<m && o<(int)sizeof esc-8;i++){
      unsigned char c=p[i];
      if(c=='\\'||c=='"'){ esc[o++]='\\'; esc[o++]=(char)c; }
      else if(c=='\n'){ esc[o++]='\\'; esc[o++]='n'; }
      else if(c=='\r'){ esc[o++]='\\'; esc[o++]='r'; }
      else if(c=='\t'){ esc[o++]='\\'; esc[o++]='t'; }
      else if(c<32) continue;
      else esc[o++]=(char)c;
    }
    esc[o]=0;
    if(!esc[0]) break;
    int i=cdp_id();
    cdp("{\"id\":%d,\"method\":\"Input.insertText\",\"params\":{\"text\":\"%s\"}}",i,esc);
    break; }
  case ASBC_MOUSE:{
    if(n<6) break;
    int x=asbc_get_u16(p)*100/g_zoom, y=asbc_get_u16(p+2)*100/g_zoom, btn=p[4], ty=p[5];
    g_mx=x; g_my=y;
    const char *type=ty==ASBC_MDOWN?"mousePressed":ty==ASBC_MUP?"mouseReleased":"mouseMoved";
    int which=btn&1?1:btn&2?2:btn&4?4: (ty==ASBC_MUP?1:0);
    const char *button=which==1?"left":which==2?"right":which==4?"middle":"none";
    int buttons=ty==ASBC_MUP?0:which;
    if(ty==ASBC_MDOWN && which==1) pw_probe(x,y,1); /* save-password bar buttons, login form submit (before the click) */
    int i=cdp_id();
    cdp("{\"id\":%d,\"method\":\"Input.dispatchMouseEvent\",\"params\":{\"type\":\"%s\",\"x\":%d,\"y\":%d,\"button\":\"%s\",\"buttons\":%d,\"clickCount\":%d}}",
        i,type,x,y,button,buttons,(ty==ASBC_MDOWN||ty==ASBC_MUP)?1:0);
    break; }
  case ASBC_WHEEL:{
    if(n<2) break;
    int dy=asbc_get_i16(p); int i=cdp_id();
    cdp("{\"id\":%d,\"method\":\"Input.dispatchMouseEvent\",\"params\":{\"type\":\"mouseWheel\",\"x\":%d,\"y\":%d,\"deltaX\":0,\"deltaY\":%d}}",
        i,g_mx,g_my,dy);
    break; }
  case ASBC_KEY:{
    if(n<6) break;
    int ty=p[0], vk=asbc_get_u16(p+1), mods=p[3], tn=asbc_get_u16(p+4);
    char text[64]; int m=tn; if(m<0) m=0; if(m>n-6) m=n-6; if(m>(int)sizeof text-1) m=(int)sizeof text-1;
    memcpy(text,p+6,(size_t)m); text[m]=0;
    char esc[128]; int o=0;
    for(int i=0;i<m&&o<(int)sizeof esc-2;i++){ if(text[i]=='\\'||text[i]=='"') esc[o++]='\\'; esc[o++]=text[i]; }
    esc[o]=0;
    if(ty==ASBC_KCHAR && esc[0] && (unsigned char)esc[0]>=32){
      int i=cdp_id();
      cdp("{\"id\":%d,\"method\":\"Input.insertText\",\"params\":{\"text\":\"%s\"}}",i,esc);
      break; }
    if(ty==ASBC_KCHAR){
      if(esc[0]=='\b') vk=8;
      else if(esc[0]=='\r'||esc[0]=='\n') vk=13;
      else if(esc[0]=='\t') vk=9;
      ty=ASBC_KDOWN;
    }
    const char *type=ty==ASBC_KUP?"keyUp":"keyDown";
    char key[24]="", code[24]="";
    if(vk>='A'&&vk<='Z'){ snprintf(key,sizeof key,"%c",(mods&8)?vk:vk+'a'-'A'); snprintf(code,sizeof code,"Key%c",vk); }
    else if(vk>='0'&&vk<='9'){ snprintf(key,sizeof key,"%c",vk); snprintf(code,sizeof code,"Digit%c",vk); }
    else if(vk==13){ snprintf(key,sizeof key,"Enter"); snprintf(code,sizeof code,"Enter"); }
    else if(vk==8){ snprintf(key,sizeof key,"Backspace"); snprintf(code,sizeof code,"Backspace"); }
    else if(vk==9){ snprintf(key,sizeof key,"Tab"); snprintf(code,sizeof code,"Tab"); }
    else if(vk==27){ snprintf(key,sizeof key,"Escape"); snprintf(code,sizeof code,"Escape"); }
    else if(vk==32){ snprintf(key,sizeof key," "); snprintf(code,sizeof code,"Space"); }
    else if(vk==37){ snprintf(key,sizeof key,"ArrowLeft"); snprintf(code,sizeof code,"ArrowLeft"); }
    else if(vk==38){ snprintf(key,sizeof key,"ArrowUp"); snprintf(code,sizeof code,"ArrowUp"); }
    else if(vk==39){ snprintf(key,sizeof key,"ArrowRight"); snprintf(code,sizeof code,"ArrowRight"); }
    else if(vk==40){ snprintf(key,sizeof key,"ArrowDown"); snprintf(code,sizeof code,"ArrowDown"); }
    else if(vk==46){ snprintf(key,sizeof key,"Delete"); snprintf(code,sizeof code,"Delete"); }
    if(vk==13 && !esc[0]){ esc[0]='\r'; esc[1]=0; }
    if(vk==13 && ty==ASBC_KDOWN) pw_probe(0,0,2); /* Enter in a login form */
    int i=cdp_id();
    cdp("{\"id\":%d,\"method\":\"Input.dispatchKeyEvent\",\"params\":{\"type\":\"%s\",\"windowsVirtualKeyCode\":%d,\"nativeVirtualKeyCode\":%d,\"modifiers\":%d,\"key\":\"%s\",\"code\":\"%s\"%s%s%s}}",
        i,type,vk,vk,mods,key,code,esc[0]?",\"text\":\"":"",esc,esc[0]?"\"":"");
    if(ty==ASBC_KDOWN && (vk==8||vk==13||vk==9||vk==46)){
      i=cdp_id();
      cdp("{\"id\":%d,\"method\":\"Input.dispatchKeyEvent\",\"params\":{\"type\":\"keyUp\",\"windowsVirtualKeyCode\":%d,\"nativeVirtualKeyCode\":%d,\"modifiers\":%d,\"key\":\"%s\",\"code\":\"%s\"}}",
          i,vk,vk,mods,key,code);
    }
    break; }
  default: break;
  }
}

static void close_cli(void){
  if(g_cli>=0){ close(g_cli); g_cli=-1; }
  audio_stop_cap();
  if(g_screencast_on) cdp_screencast(0);
}

/* is an X server really listening on /tmp/.X11-unix/X<n>? (a stale socket file is not enough) */
static int x_alive(const char *path){
  int s=socket(AF_UNIX,SOCK_STREAM,0); if(s<0) return 0;
  struct sockaddr_un u; memset(&u,0,sizeof u); u.sun_family=AF_UNIX; scopy(u.sun_path,sizeof u.sun_path,path);
  int ok=connect(s,(struct sockaddr*)&u,sizeof u)==0; close(s); return ok; }
static void ensure_xvfb(void){
  const char *d=getenv("ASB_CHROME_DISPLAY");
  if(d&&d[0]){ setenv("DISPLAY",d,1); return; }
  if(x_alive("/tmp/.X11-unix/X78")){ setenv("DISPLAY",":78",1); return; }
  unlink("/tmp/.X11-unix/X78"); unlink("/tmp/.X78-lock");
  pid_t x=fork();
  if(x==0){
    int n=open("/dev/null",O_RDWR); if(n>=0){ dup2(n,0); dup2(n,1); dup2(n,2); }
    execlp("Xvfb","Xvfb",":78","-screen","0","1920x1080x24","-nolisten","tcp",(char*)0);
    _exit(127);
  }
  for(int i=0;i<50;i++){ if(x_alive("/tmp/.X11-unix/X78")) break; usleep(100000); }
  setenv("DISPLAY",":78",1);
}
/* major version from "chrome --version" ("Google Chrome 155.0.8059.84", "Chromium 155.0.8059.79") */
static int chrome_major(const char *bin){
  char cmd[700]; snprintf(cmd,sizeof cmd,"'%s' --version 2>/dev/null",bin);
  if(strchr(bin,'\'')) return 0;
  FILE *f=popen(cmd,"r"); if(!f) return 0;
  char l[200]=""; if(!fgets(l,sizeof l,f)) l[0]=0; pclose(f);
  for(char *p=l;*p;p++) if(*p>='1'&&*p<='9'&&(p==l||p[-1]==' ')) return atoi(p);
  return 0; }
/* 0.80 Widevine (DRM: Netflix, Spotify, Prime, Disney+). Google Chrome brings its own CDM. Chromium / ungoogled-chromium
   load one only through the "component updated" hint file in the profile (user-data-dir/WidevineCdm/
   latest-component-updated-widevine-cdm = {"Path": dir}); a WidevineCdm folder next to the binary is ignored. The CDM is
   Google's and may not be redistributed, so we never ship it: we point the hint at a copy that is already on this PC
   (ASB_WIDEVINE_DIR - AmiKit: Host/data/WidevineCdm, filled by get-widevine.sh -, Google Chrome's, or the usual
   Chromium locations). ASB_WIDEVINE=0 turns it off. */
static int wv_ok(const char *d,char *ver,size_t vc){
  char f[700]; struct stat st;
  snprintf(f,sizeof f,"%s/_platform_specific/linux_x64/libwidevinecdm.so",d); if(stat(f,&st)!=0||st.st_size<100000) return 0;
  snprintf(f,sizeof f,"%s/manifest.json",d); FILE *m=fopen(f,"r"); if(!m) return 0;
  char b[4096]; size_t n=fread(b,1,sizeof b-1,m); fclose(m); b[n]=0;
  char *v=strstr(b,"\"version\""); ver[0]=0;
  if(v && (v=strchr(v+9,'"'))){ v++; char *e=strchr(v,'"'); if(e && (size_t)(e-v)<vc){ memcpy(ver,v,(size_t)(e-v)); ver[e-v]=0; } }
  return ver[0]!=0; }
static void widevine_hint(const char *bin,const char *profile){
  const char *off=getenv("ASB_WIDEVINE"); if(off && off[0]=='0') return;
  if(strstr(bin,"google/chrome") || strstr(bin,"google-chrome")){ fprintf(stderr,"asb-chromed: Widevine: Google Chrome's own\n"); return; }
  const char *home=getenv("HOME"); char h1[600]="", h2[600]="";
  if(home){ snprintf(h1,sizeof h1,"%s/.local/lib/WidevineCdm",home); snprintf(h2,sizeof h2,"%s/.config/chromium/WidevineCdm/current",home); }
  const char *c[]={getenv("ASB_WIDEVINE_DIR"),"/opt/google/chrome/WidevineCdm","/opt/google/chrome-beta/WidevineCdm",
    "/opt/google/chrome-unstable/WidevineCdm",h1,h2,"/usr/lib/chromium/WidevineCdm","/usr/lib/chromium-browser/WidevineCdm",
    "/usr/lib64/chromium-browser/WidevineCdm"};
  char ver[64], real[PATH_MAX];
  for(size_t i=0;i<sizeof c/sizeof c[0];i++){
    if(!c[i]||!c[i][0]||!wv_ok(c[i],ver,sizeof ver)) continue;
    if(!realpath(c[i],real)) scopy(real,sizeof real,c[i]);   /* the hint must name a real folder, not a symlink */
    if(strchr(real,'"')||strchr(real,'\\')) continue;
    char d[700], f[760]; snprintf(d,sizeof d,"%s/WidevineCdm",profile); mkdir(profile,0700); mkdir(d,0700);
    snprintf(f,sizeof f,"%s/latest-component-updated-widevine-cdm",d);
    FILE *o=fopen(f,"w"); if(!o){ fprintf(stderr,"asb-chromed: Widevine: cannot write %s\n",f); return; }
    fprintf(o,"{\"Path\":\"%s\"}",real); fclose(o);
    fprintf(stderr,"asb-chromed: Widevine %s from %s (DRM video: Netflix, Spotify ...)\n",ver,real); return; }
  fprintf(stderr,"asb-chromed: Widevine: none found - DRM sites (Netflix, Spotify, Prime, Disney+) will not play;"
    " AmiKit edition: run Host/get-widevine.sh once, or install Google Chrome\n"); }
static int spawn_chrome(const char *profile, int dbgport){
  const char *bin=getenv("ASB_CHROME");
  if(!bin||!bin[0]){
    if(!access("/usr/bin/google-chrome-stable",X_OK)) bin="/usr/bin/google-chrome-stable";
    else if(!access("/usr/bin/google-chrome",X_OK)) bin="/usr/bin/google-chrome";
    else if(!access("/usr/bin/chromium-browser",X_OK)) bin="/usr/bin/chromium-browser";
    else { fprintf(stderr,"asb-chromed: no Chrome binary\n"); return -1; }
  }
  widevine_hint(bin,profile);
  const char *hl=getenv("ASB_HEADLESS"); int headless=hl&&hl[0]=='1'; /* no X server needed (AmiKit bundle) */
  if(!headless) ensure_xvfb();
  fprintf(stderr,"asb-chromed: Chrome %s%s\n",headless?"headless":"DISPLAY=",headless?"":getenv("DISPLAY"));
  char portf[48], sizef[64], udir[600];
  snprintf(portf,sizeof portf,"--remote-debugging-port=%d",dbgport);
  snprintf(sizef,sizeof sizef,"--window-size=%d,%d",g_vw,g_vh);
  snprintf(udir,sizeof udir,"--user-data-dir=%s",profile);
  char dldir[600]; snprintf(dldir,sizeof dldir,"--download-default-directory=%s",g_dlpath[0]?g_dlpath:"/tmp");
  /* Headless Chrome says "HeadlessChrome/NNN" in its user agent, and Google's sign-in refuses it ("This browser or
     app may not be secure"). Give it the same UA a normal Linux Chrome of that version sends (reduced UA format). */
  char uaf[300]=""; if(headless){ int major=chrome_major(bin); if(major<=0) major=140;
    snprintf(uaf,sizeof uaf,"--user-agent=Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/%d.0.0.0 Safari/537.36",major); }
  pid_t pid=fork();
  if(pid<0) return -1;
  if(pid==0){
    int n=open("/dev/null",O_RDWR); if(n>=0){ dup2(n,0); dup2(n,1); }
    execl(bin,bin,
      portf, udir, sizef,
      "--window-position=0,0",
      "--remote-allow-origins=*",
      "--disable-blink-features=AutomationControlled",
      "--exclude-switches=enable-automation",
      "--no-first-run","--no-default-browser-check","--disable-sync",
      "--autoplay-policy=no-user-gesture-required",
      dldir,
      headless?"--headless=new":(char*)0, headless?"--hide-scrollbars":(char*)0, headless?uaf:(char*)0,
      (char*)0);
    _exit(127);
  }
  g_chrome=pid;
  return 0;
}

static void audio_cleanup(void);
static void die_chrome(int sig){ (void)sig; if(g_chrome>0) kill(g_chrome,SIGTERM); audio_cleanup(); _exit(0); }

static int wait_port(int port, int ms){
  int t0=0;
  while(t0<ms){
    int s=tcp_connect_local(port);
    if(s>=0){ close(s); return 0; }
    usleep(100000); t0+=100;
  }
  return -1;
}

static char g_profile[512]; static int g_want=1, g_bidiport;
static void browser_stop(void){
  if(g_screencast_on) cdp_screencast(0);
  if(g_ws>=0){ close(g_ws); g_ws=-1; }
  if(g_bcon>=0){ close(g_bcon); g_bcon=-1; }
  if(g_chrome>0){ pid_t pid=g_chrome; kill(pid,SIGTERM); /* our own child only, by its PID */
    for(int i=0;i<50 && kill(pid,0)==0;i++) usleep(100000);
    if(kill(pid,0)==0) kill(pid,SIGKILL); g_chrome=0; }
  g_ntab=0; g_norder=0; g_tab=0; g_ctx[0]=0; g_npre=0; g_bidi_dl=0; g_cast_id=0; g_bidi=0;
  free(g_prev); g_prev=NULL; }
/* kind 1 Chrome (CDP), 2 Firefox (WebDriver BiDi) */
static int browser_start(int kind){
  if(kind==2){
    char fp[600]; snprintf(fp,sizeof fp,"%s-firefox",g_profile);
    if(spawn_firefox(fp,g_bidiport)) return -1;
    if(wait_port(g_bidiport,20000)){ fprintf(stderr,"asb-chromed: Firefox BiDi port %d did not open\n",g_bidiport); return -1; }
    g_bidi=1;
    if(bidi_session(g_bidiport)){ fprintf(stderr,"asb-chromed: Firefox BiDi session failed\n"); return -1; }
    cdp_enable(); cdp_viewport(g_vw,g_vh);
    return 0; }
  g_bidi=0;
  { int t=tcp_connect_local(g_dbg); if(t>=0){ close(t); /* never attach to someone else's Chrome */
      fprintf(stderr,"asb-chromed: Chrome debug port %d is already in use by another program; pick another with -cdp\n",g_dbg); return -1; } }
  if(spawn_chrome(g_profile,g_dbg)) return -1;
  if(wait_port(g_dbg,15000)){ fprintf(stderr,"asb-chromed: Chrome debug port %d did not open\n",g_dbg); return -1; }
  /* pick a real tab (type "page", not an extension/background/devtools target); Chrome may list its tab late */
  char wsurl[512]; wsurl[0]=0;
  for(int tries=0;tries<50 && !wsurl[0];tries++){
    if(load_tabs()>0){ g_tab=0; scopy(wsurl,sizeof wsurl,g_tabs[0].ws); break; }
    usleep(200000);
  }
  if(!wsurl[0]){ fprintf(stderr,"asb-chromed: no page tab found in /json/list\n"); return -1; }
  fprintf(stderr,"asb-chromed: ws %s\n",wsurl);
  g_ws=ws_open(wsurl); if(g_ws<0){ fprintf(stderr,"asb-chromed: ws_open failed\n"); return -1; }
  cdp_enable();
  cdp_viewport(g_vw,g_vh);
  return 0; }
static int browser_switch(int want){
  if(want!=1 && want!=2) return 0;
  char u[2400]; scopy(u,sizeof u,g_url);
  send_status(want==2?"Starting Firefox...":"Starting Chrome...");
  browser_stop();
  if(browser_start(want)){
    send_status(want==2?"Firefox did not start; using Chrome":"Chrome did not start; using Firefox");
    browser_stop(); if(browser_start(want==2?1:2)) return -1; }
  g_want=want;
  if(u[0] && strcmp(u,"about:blank")) cdp_nav(u);
  send_status(g_bidi?"Firefox (WebDriver BiDi)":"Chrome (CDP)");
  return 0; }

static int g_idle_min; /* -idle N: quit after N minutes without a client (0 = never) */
int main(int argc, char **argv){
  int dbg=9222, asbc=ASBC_PORT;
  const char *home=getenv("HOME"); if(!home) home="/tmp";
  char profile[512]; snprintf(profile,sizeof profile,"%s/.config/amistudio-chrome",home);
  const char *start=NULL, *bindaddr=NULL, *token=getenv("ASB_TOKEN");
  for(int i=1;i<argc;i++){
    if(!strcmp(argv[i],"-port")&&i+1<argc) asbc=atoi(argv[++i]);
    else if(!strcmp(argv[i],"-cdp")&&i+1<argc) dbg=atoi(argv[++i]);
    else if(!strcmp(argv[i],"-bidi")&&i+1<argc) g_bidiport=atoi(argv[++i]);
    else if(!strcmp(argv[i],"-browser")&&i+1<argc){ const char *b=argv[++i]; if(!strcasecmp(b,"firefox")) g_want=2; else if(!strcasecmp(b,"chrome")) g_want=1; else { fprintf(stderr,"asb-chromed: -browser firefox|chrome\n"); return 2; } }
    else if(!strcmp(argv[i],"-listen")&&i+1<argc) bindaddr=argv[++i];
    else if(!strcmp(argv[i],"-token")&&i+1<argc) token=argv[++i];
    else if(!strcmp(argv[i],"-h")||!strcmp(argv[i],"--help")){
      fprintf(stderr,"usage: asb-chromed [-browser chrome|firefox] [-port 9223] [-cdp 9222] [-bidi 9322] [-profile DIR] [-idle MIN] [-listen ADDR -token SECRET] [URL]\n"
        "  default: ASBC on 127.0.0.1 only. -listen 0.0.0.0 (or a LAN address) needs -token (or ASB_TOKEN).\n"); return 0; }
    else if(!strcmp(argv[i],"-idle")&&i+1<argc) g_idle_min=atoi(argv[++i]);
    else if(!strcmp(argv[i],"-profile")&&i+1<argc) snprintf(profile,sizeof profile,"%s",argv[++i]);
    else if(argv[i][0]!='-') start=argv[i];
  }
  if(bindaddr && strcmp(bindaddr,"127.0.0.1") && (!token || strlen(token)<8)){
    fprintf(stderr,"asb-chromed: -listen %s exposes remote control of Chrome; give -token with at least 8 characters (or ASB_TOKEN)\n",bindaddr); return 2; }
  if(token && strlen(token)>ASBC_MAX_TOKEN){ fprintf(stderr,"asb-chromed: token longer than %d\n",ASBC_MAX_TOKEN); return 2; }
  signal(SIGPIPE,SIG_IGN);
  signal(SIGINT,die_chrome); signal(SIGTERM,die_chrome);
  signal(SIGCHLD,SIG_IGN);

  g_dbg=dbg; if(!g_bidiport) g_bidiport=dbg+100;
  feat_init(asbc);
  const char *dlenv=getenv("ASB_DOWNLOADS");
  if(dlenv&&dlenv[0]) snprintf(g_dlpath,sizeof g_dlpath,"%s",dlenv);
  else if(access("/opt/amikit/AmiKit",F_OK)==0)
    snprintf(g_dlpath,sizeof g_dlpath,"/opt/amikit/AmiKit/Downloads");
  else snprintf(g_dlpath,sizeof g_dlpath,"%s/amikit/AmiKit/Downloads",home);
  mkdir(g_dlpath,0755);
  fprintf(stderr,"asb-chromed: profile %s downloads %s\n",profile,g_dlpath);
  scopy(g_profile,sizeof g_profile,profile);
  /* claim the ASBC port before starting a browser: a second copy (AmiKit auto-start, a double click) exits quietly */
  g_listen=tcp_listen_local(asbc,bindaddr);
  if(g_listen<0){ int t=tcp_connect_local(asbc);
    if(t>=0){ close(t); fprintf(stderr,"asb-chromed: already running on port %d, nothing to do\n",asbc); return 0; }
    perror("listen"); return 1; }
  audio_setup();
  if(browser_start(g_want)){
    if(g_want==2){ fprintf(stderr,"asb-chromed: Firefox failed to start, using Chrome\n"); browser_stop(); if(browser_start(1)) return 1; }
    else return 1; }
  if(start) cdp_nav(start);
  fprintf(stderr,"asb-chromed: ASBC on 127.0.0.1:%d\n",asbc);
  send_status("waiting for client");

  buf_t wsbuf={0};
  unsigned char msg[65536];
  for(;;){
    fd_set rf; FD_ZERO(&rf);
    int m=g_listen; FD_SET(g_listen,&rf);
    if(g_ws>=0){ FD_SET(g_ws,&rf); if(g_ws>m) m=g_ws; }
    if(g_bcon>=0){ FD_SET(g_bcon,&rf); if(g_bcon>m) m=g_bcon; }
    if(g_cli>=0){ FD_SET(g_cli,&rf); if(g_cli>m) m=g_cli; }
    if(g_pagefd>=0){ FD_SET(g_pagefd,&rf); if(g_pagefd>m) m=g_pagefd; }
    if(g_au_fd>=0){ FD_SET(g_au_fd,&rf); if(g_au_fd>m) m=g_au_fd; }
    struct timeval tv={0,20000};
    int n=select(m+1,&rf,NULL,NULL,&tv);
    if(n<0){ if(errno==EINTR) continue; break; }
    if(g_chrome>0){ int st; waitpid(g_chrome,&st,WNOHANG); }
    stats_send(); audio_pump(); vq_pump();
    bidi_cast_tick(); kick_tick();
    if(g_idle_min>0){ static long long since; long long t=now_ms();
      if(g_cli>=0) since=0; else if(!since) since=t;
      else if(t-since>(long long)g_idle_min*60000){ fprintf(stderr,"asb-chromed: no client for %d min, exiting\n",g_idle_min); browser_stop(); return 0; } }
    bcon_tick();
    if(g_bidi && g_tabs_dirty && g_cli>=0){ g_tabs_dirty=0; load_tabs();
      if(tab_by_id(g_ctx)<0 && g_ntab>0) attach_tab(g_ntab-1); else { int k=tab_by_id(g_ctx); if(k>=0) g_tab=k; send_tabs(); } }
    if(g_shot_due && now_ms()>=g_shot_due){ g_shot_due=0;
      if(!g_frames_seen && g_cli>=0 && g_ws>=0){ g_shot_id=cdp_id(); cdp("{\"id\":%d,\"method\":\"Page.captureScreenshot\",\"params\":{\"format\":\"png\"}}",g_shot_id); } }
    /* titles of single-page apps (YouTube, Gmail) change without a load event: refresh URL+title every 2 s */
    { static time_t last; time_t now=time(NULL);
      if(g_cli>=0 && g_ws>=0 && now-last>=2){ last=now; int i=cdp_id();
        cdp("{\"id\":%d,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":\"location.href+'\\\\n'+document.title+'\\\\n'+document.readyState\",\"returnByValue\":true}}",i); } }

    if(g_pagefd>=0 && FD_ISSET(g_pagefd,&rf)){ int c=accept(g_pagefd,NULL,NULL); if(c>=0) page_serve(c); }
    if(FD_ISSET(g_listen,&rf)){
      struct sockaddr_in a; socklen_t al=sizeof a;
      int c=accept(g_listen,(struct sockaddr*)&a,&al);
      if(c>=0 && ban_check(a.sin_addr.s_addr)){ close(c); c=-1; }
      if(c>=0){
        int one=1; setsockopt(c,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);
        struct timeval hto={3,0}; setsockopt(c,SOL_SOCKET,SO_RCVTIMEO,&hto,sizeof hto);
        int hs=asbc_handshake_server(c,token);
        hto.tv_sec=0; setsockopt(c,SOL_SOCKET,SO_RCVTIMEO,&hto,sizeof hto);
        if(hs){ static time_t lastlog; static int quiet; time_t now=time(NULL);
          if(now!=lastlog){ fprintf(stderr,"asb-chromed: client %s rejected (%s)%s\n",inet_ntoa(a.sin_addr),hs==-2?"bad token":"bad handshake",quiet?" [more suppressed]":""); lastlog=now; quiet=0; } else quiet++;
          ban_fail(a.sin_addr.s_addr); close(c); }
        else { if(g_cli>=0){ const char *rm="replaced by another client"; asbc_send(g_cli,ASBC_STATUS,rm,(uint32_t)strlen(rm)); close_cli(); } g_cli=c; g_bm_want=0; pf_reset(); inflight_reset(); g_vlat_ms=0; g_avdelay_ms=0; vq_clear(); free(g_pend); g_pend=NULL; fprintf(stderr,"asb-chromed: client connected\n"); send_status("connected"); }
      }
    }
    if(g_cli>=0 && FD_ISSET(g_cli,&rf)){
      for(int nmsg=0;nmsg<32;nmsg++){
        int op=0; int k;
        if(nmsg==0) k=asbc_recv(g_cli,&op,msg,sizeof msg);
        else k=asbc_recv_nb(g_cli,&op,msg,sizeof msg);
        if(k==0 && op<0) break;
        if(k<0){ fprintf(stderr,"asb-chromed: client gone\n"); close_cli(); break; }
        on_client(op,msg,k);
      }
    }
    if(g_ws>=0 && FD_ISSET(g_ws,&rf)){
      int k=ws_recv(g_ws,&wsbuf);
      if(k<0){ fprintf(stderr,"asb-chromed: chrome ws closed\n"); close(g_ws); g_ws=-1; g_screencast_on=0; }
      else if(k>0 && wsbuf.n){ if(g_bidi) on_bidi(wsbuf.p); else on_cdp(wsbuf.p); }
    }
    if(g_bcon>=0 && FD_ISSET(g_bcon,&rf)){
      static buf_t bbuf;
      int k=ws_recv(g_bcon,&bbuf);
      if(k<0){ fprintf(stderr,"asb-chromed: browser ws closed\n"); close(g_bcon); g_bcon=-1; g_bcon_try=now_ms(); }
      else if(k>0 && bbuf.n) on_bcon(bbuf.p);
    }
  }
  close_cli(); audio_cleanup();
  if(g_ws>=0) close(g_ws);
  if(g_listen>=0) close(g_listen);
  if(g_chrome>0) kill(g_chrome,SIGTERM);
  return 0;
}
