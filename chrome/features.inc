/* asb-chromed browser features (0.68): zoom, find count, context info, save/print, view source,
   tab order + reopen, bookmarks/history/downloads/settings/passwords pages, suggestions.
   Included from host.c after the tab helpers. Pages are served on 127.0.0.1:<asbc+1> and rendered by Chrome,
   so both clients get them for free. */
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <ctype.h>

static char g_cfg[600];              /* ~/.config/amisurf (0.81 and older: ~/.config/amistudio-browser, still used if only that exists) */
/* g_zoom, g_order: declared in host.c */
static int g_pageport, g_pagefd=-1;
static char g_pagekey[33];           /* per-run secret for state-changing page links (CSRF) */
static char g_search[600]="https://html.duckduckgo.com/html/?q=%s";
static char g_homepage[1200]="https://www.google.com/";
static int g_defzoom=100;
static int g_findcur, g_findtot;
static char g_closed[16][2400]; static int g_nclosed;

/* pending CDP replies we care about */
enum { K_NONE, K_FINDCOUNT, K_CTX, K_SAVEPNG, K_SAVEHTML, K_SAVEPDF, K_PWORIGIN, K_PWPROBE };
static struct { int id, kind; char arg[160]; } g_req[32];
static void req_add(int id,int kind,const char *arg){
  for(int i=0;i<32;i++) if(!g_req[i].id){ g_req[i].id=id; g_req[i].kind=kind; scopy(g_req[i].arg,sizeof g_req[i].arg,arg?arg:""); return; }
  g_req[0].id=id; g_req[0].kind=kind; }

/* ---- small helpers ---- */
static void jesc(const char *s, char *o, int cap){ /* JSON+JS string escape (no quotes added) */
  int n=0;
  for(;*s && n<cap-7;s++){ unsigned char c=(unsigned char)*s;
    if(c=='\\'||c=='"'||c=='\''){ o[n++]='\\'; o[n++]=(char)c; }
    else if(c=='<'){ memcpy(o+n,"\\u003c",6); n+=6; }
    else if(c=='\n'){ o[n++]='\\'; o[n++]='n'; }
    else if(c<32) continue;
    else o[n++]=(char)c; }
  o[n]=0; }
static void hesc(const char *s, char *o, int cap){ /* HTML escape */
  int n=0;
  for(;*s && n<cap-7;s++){
    if(*s=='<'){ memcpy(o+n,"&lt;",4); n+=4; } else if(*s=='>'){ memcpy(o+n,"&gt;",4); n+=4; }
    else if(*s=='&'){ memcpy(o+n,"&amp;",5); n+=5; } else if(*s=='"'){ memcpy(o+n,"&quot;",6); n+=6; }
    else o[n++]=*s; }
  o[n]=0; }
static void uenc(const char *s, char *o, int cap){
  int n=0; static const char *hx="0123456789ABCDEF";
  for(;*s && n<cap-4;s++){ unsigned char c=(unsigned char)*s;
    if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~') o[n++]=(char)c;
    else { o[n++]='%'; o[n++]=hx[c>>4]; o[n++]=hx[c&15]; } }
  o[n]=0; }
static void udec(char *s){
  char *o=s;
  for(;*s;s++){
    if(*s=='+') *o++=' ';
    else if(*s=='%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])){ char h[3]={s[1],s[2],0}; *o++=(char)strtol(h,NULL,16); s+=2; }
    else *o++=*s; }
  *o=0; }
static int qparam(const char *q, const char *key, char *out, int cap){ /* from "a=1&b=2" */
  size_t kl=strlen(key); const char *p=q;
  while(p && *p){
    if(!strncmp(p,key,kl) && p[kl]=='='){ p+=kl+1; int n=0; while(*p && *p!='&' && n<cap-1) out[n++]=*p++; out[n]=0; udec(out); return 1; }
    p=strchr(p,'&'); if(p) p++; }
  out[0]=0; return 0; }
static int is_internal(const char *u){
  char pre[64]; snprintf(pre,sizeof pre,"http://127.0.0.1:%d/",g_pageport);
  return !strncmp(u,pre,strlen(pre)); }
static int skip_url(const char *u){
  return !u[0]||!strncmp(u,"about:",6)||!strncmp(u,"chrome",6)||!strncmp(u,"view-source:",12)||!strncmp(u,"data:",5)||!strncmp(u,"devtools:",9)||is_internal(u); }
static void cfgpath(char *o,int cap,const char *name){ snprintf(o,(size_t)cap,"%s/%s",g_cfg,name); }
static void safe_name(const char *in, char *o, int cap, const char *ext){
  int n=0; for(;*in && n<cap-12;in++){ unsigned char c=(unsigned char)*in; o[n++]=(isalnum(c)||c=='-'||c=='_'||c=='.')?(char)c:'_'; }
  if(!n){ time_t t=time(NULL); n=(int)strftime(o,(size_t)cap-8,"page-%Y%m%d-%H%M%S",localtime(&t)); }
  o[n]=0; if(n>60) o[60]=0;
  if(ext && !strstr(o,ext)) strncat(o,ext,(size_t)cap-strlen(o)-1); }
static void unique_path(char *o,int cap,const char *dir,const char *name){
  snprintf(o,(size_t)cap,"%s/%s",dir,name);
  for(int i=2;i<100 && access(o,F_OK)==0;i++){
    const char *dot=strrchr(name,'.'); int bl=dot?(int)(dot-name):(int)strlen(name);
    snprintf(o,(size_t)cap,"%s/%.*s-%d%s",dir,bl,name,i,dot?dot:""); } }

/* ---- settings ---- */
static int settings_save(void){
  char p[700]; cfgpath(p,sizeof p,"settings"); FILE *f=fopen(p,"w"); if(!f) return -1;
  fprintf(f,"home=%s\nsearch=%s\nzoom=%d\n",g_homepage,g_search,g_defzoom); return fclose(f)==0?1:-1; }
static void settings_load(void){
  char p[700]; cfgpath(p,sizeof p,"settings"); FILE *f=fopen(p,"r"); if(!f) return;
  char l[1400];
  while(fgets(l,sizeof l,f)){ l[strcspn(l,"\r\n")]=0;
    if(!strncmp(l,"home=",5)) scopy(g_homepage,sizeof g_homepage,l+5);
    else if(!strncmp(l,"search=",7) && strstr(l,"%s")) scopy(g_search,sizeof g_search,l+7);
    else if(!strncmp(l,"zoom=",5)){ int z=atoi(l+5); if(z>=25&&z<=400) g_defzoom=z; } }
  fclose(f); g_zoom=g_defzoom; }

/* ---- bookmarks / history: TSV "time\turl\ttitle" (0.87: bookmarks may have a 4th column, the folder; an older
   engine reads it as part of the title and keeps it when it rewrites the file) ---- */
#define LMAX 2000
typedef struct { long t; char url[1200]; char title[200]; char folder[64]; } ent_t;
static int list_read(const char *name, ent_t *e, int cap){
  char p[700]; cfgpath(p,sizeof p,name); FILE *f=fopen(p,"r"); if(!f) return 0;
  char l[1600]; int n=0;
  while(fgets(l,sizeof l,f)){
    l[strcspn(l,"\r\n")]=0; char *a=strchr(l,'\t'); if(!a) continue; *a++=0; char *b=strchr(a,'\t'); if(b) *b++=0;
    char *c=b?strchr(b,'\t'):NULL; if(c) *c++=0;
    if(n>=cap){ memmove(e,e+1,sizeof(ent_t)*(size_t)(cap-1)); n=cap-1; }
    e[n].t=atol(l); scopy(e[n].url,sizeof e[n].url,a); scopy(e[n].title,sizeof e[n].title,b?b:""); scopy(e[n].folder,sizeof e[n].folder,c?c:""); n++; }
  fclose(f); return n; }
static void list_write(const char *name, ent_t *e, int n){
  char p[700], t[720]; cfgpath(p,sizeof p,name); snprintf(t,sizeof t,"%s.tmp",p);
  FILE *f=fopen(t,"w"); if(!f) return;
  for(int i=0;i<n;i++){ if(e[i].folder[0]) fprintf(f,"%ld\t%s\t%s\t%s\n",e[i].t,e[i].url,e[i].title,e[i].folder); else fprintf(f,"%ld\t%s\t%s\n",e[i].t,e[i].url,e[i].title); }
  fclose(f); rename(t,p); }
static void clean_field(char *s){ for(;*s;s++) if(*s=='\t'||*s=='\n'||*s=='\r') *s=' '; }
static void list_append(const char *name,const char *url,const char *title){
  char p[700]; cfgpath(p,sizeof p,name); FILE *f=fopen(p,"a"); if(!f) return;
  char u[1200], t[200]; scopy(u,sizeof u,url); scopy(t,sizeof t,title?title:""); clean_field(u); clean_field(t);
  fprintf(f,"%ld\t%s\t%s\n",(long)time(NULL),u,t); fclose(f); }
static void hist_note(void){ /* called from send_meta */
  static char last[2400];
  if(g_loading || skip_url(g_url) || !strcmp(last,g_url)) return;
  scopy(last,sizeof last,g_url); list_append("history.tsv",g_url,g_title);
  /* keep history bounded */
  static int adds; if(++adds%200==0){ static ent_t e[LMAX]; int n=list_read("history.tsv",e,LMAX); list_write("history.tsv",e,n); } }
/* 0.87: the bookmark list for the client's bookmarks bar and menu (ASBC_BMARKS), once it asked ("bookmarks") */
static int g_bm_want;
static void send_bmarks(void){
  if(g_cli<0 || !g_bm_want) return;
  static ent_t e[LMAX]; int n=list_read("bookmarks.tsv",e,LMAX);
  size_t cap=64*1024, o=0; char *b=malloc(cap); if(!b) return;
  for(int i=0;i<n && i<500;i++){ int m=snprintf(b+o,cap-o,"%s\t%s\t%s\n",e[i].url,e[i].title,e[i].folder); if(m<0||(size_t)m>=cap-o) break; o+=(size_t)m; }
  asbc_send(g_cli,ASBC_BMARKS,b,(uint32_t)o); free(b); }
static void bookmark_current(void){
  if(skip_url(g_url)){ send_status("Cannot bookmark this page"); return; }
  static ent_t e[LMAX]; int n=list_read("bookmarks.tsv",e,LMAX);
  for(int i=0;i<n;i++) if(!strcmp(e[i].url,g_url)){ send_status("Already bookmarked"); return; }
  list_append("bookmarks.tsv",g_url,g_title); send_status("Bookmarked"); send_bmarks(); }
static int ci_has(const char *h,const char *n){ if(!n[0]) return 1; return strcasestr(h,n)!=NULL; }
static void suggest(const char *pre){
  if(g_cli<0) return;
  static ent_t b[LMAX], h[LMAX]; int nb=list_read("bookmarks.tsv",b,LMAX), nh=list_read("history.tsv",h,LMAX);
  char out[6000]; int o=0, cnt=0; char seen[8][1200];
  if(!pre[0]){ asbc_send(g_cli,ASBC_SUGG,"",0); return; }
  for(int pass=0;pass<2 && cnt<8;pass++){
    ent_t *e=pass?h:b; int n=pass?nh:nb;
    for(int k=0;k<n && cnt<8;k++){ int i=pass?n-1-k:k;
      if(!ci_has(e[i].url,pre) && !ci_has(e[i].title,pre)) continue;
      int dup=0; for(int j=0;j<cnt;j++) if(!strcmp(seen[j],e[i].url)) dup=1; if(dup) continue;
      scopy(seen[cnt],sizeof seen[0],e[i].url); cnt++;
      o+=snprintf(out+o,sizeof out-(size_t)o,"%s\t%s%s\n",e[i].url,pass?"":"\xe2\x98\x85 ",e[i].title);
      if(o>(int)sizeof out-1400) break; } }
  asbc_send(g_cli,ASBC_SUGG,out,(uint32_t)o); }

/* ---- downloads ---- */
typedef struct { char guid[64], name[256], url[1200]; double got, total; int state; long t; } dl_t; /* state 0 run 1 done 2 cancel */
static dl_t g_dl[32]; static int g_ndl;
/* where downloads land, for messages: the folder, plus the Amiga name when it is AmiKit's Downloads drawer */
static const char *dl_where(void){ static char w[600]; const char *h=getenv("HOME"); size_t hl=h?strlen(h):0;
  const char *p=g_dlpath[0]?g_dlpath:"/tmp"; int ak=strstr(p,"/AmiKit/Downloads")!=NULL;
  if(hl && !strncmp(p,h,hl) && p[hl]=='/') snprintf(w,sizeof w,"~%s%s",p+hl,ak?" = AmiKit:Downloads":" on the Host PC");
  else snprintf(w,sizeof w,"%s%s",p,ak?" = AmiKit:Downloads":" on the Host PC");
  return w; }
static dl_t *dl_find(const char *guid){ for(int i=0;i<g_ndl;i++) if(!strcmp(g_dl[i].guid,guid)) return &g_dl[i]; return NULL; }
static void dl_finish(dl_t *d){
  char p[700]; cfgpath(p,sizeof p,"downloads.tsv"); FILE *f=fopen(p,"a");
  if(f){ fprintf(f,"%ld\t%s\t%s%s\n",(long)time(NULL),d->url,d->name,d->state==2?" (failed or canceled)":""); fclose(f); } }

/* ---- passwords: AES-256-GCM file, key in a 0600 key file next to it ---- */
static int pw_key(unsigned char k[32]){
  char p[700]; cfgpath(p,sizeof p,"key.bin");
  FILE *f=fopen(p,"rb"); if(f){ size_t n=fread(k,1,32,f); fclose(f); if(n==32) return 0; }
  if(RAND_bytes(k,32)!=1) return -1;
  int fd=open(p,O_WRONLY|O_CREAT|O_TRUNC,0600); if(fd<0) return -1;
  int ok=write(fd,k,32)==32; close(fd); return ok?0:-1; }
static char *pw_load(void){ /* returns malloc'd plaintext TSV "origin\tuser\tpass\n" (or "never\torigin\t\n") */
  unsigned char k[32]; if(pw_key(k)) return NULL;
  char p[700]; cfgpath(p,sizeof p,"passwords.dat");
  FILE *f=fopen(p,"rb"); if(!f) return calloc(1,1);
  fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
  if(n<28||n>4000000){ fclose(f); return calloc(1,1); }
  unsigned char *b=malloc((size_t)n); if(!b||fread(b,1,(size_t)n,f)!=(size_t)n){ fclose(f); free(b); return calloc(1,1); } fclose(f);
  char *out=calloc(1,(size_t)n); int ol=0, fl=0; int ok=0;
  EVP_CIPHER_CTX *c=EVP_CIPHER_CTX_new();
  if(c && out && EVP_DecryptInit_ex(c,EVP_aes_256_gcm(),NULL,k,b)==1 &&
     EVP_DecryptUpdate(c,(unsigned char*)out,&ol,b+28,(int)n-28)==1 &&
     EVP_CIPHER_CTX_ctrl(c,EVP_CTRL_GCM_SET_TAG,16,b+12)==1 &&
     EVP_DecryptFinal_ex(c,(unsigned char*)out+ol,&fl)==1) ok=1;
  EVP_CIPHER_CTX_free(c); free(b);
  if(!ok){ free(out); fprintf(stderr,"asb-chromed: passwords.dat failed to decrypt\n"); return calloc(1,1); }
  out[ol+fl]=0; return out; }
static void pw_save(const char *plain){
  unsigned char k[32], iv[12], tag[16]; if(pw_key(k)||RAND_bytes(iv,12)!=1) return;
  int n=(int)strlen(plain); unsigned char *ct=malloc((size_t)n+16); int cl=0, fl=0;
  EVP_CIPHER_CTX *c=EVP_CIPHER_CTX_new();
  if(!c||!ct||EVP_EncryptInit_ex(c,EVP_aes_256_gcm(),NULL,k,iv)!=1||EVP_EncryptUpdate(c,ct,&cl,(const unsigned char*)plain,n)!=1||
     EVP_EncryptFinal_ex(c,ct+cl,&fl)!=1||EVP_CIPHER_CTX_ctrl(c,EVP_CTRL_GCM_GET_TAG,16,tag)!=1){ EVP_CIPHER_CTX_free(c); free(ct); return; }
  EVP_CIPHER_CTX_free(c);
  char p[700]; cfgpath(p,sizeof p,"passwords.dat");
  int fd=open(p,O_WRONLY|O_CREAT|O_TRUNC,0600);
  if(fd>=0){ if(write(fd,iv,12)!=12||write(fd,tag,16)!=16||write(fd,ct,(size_t)(cl+fl))!=cl+fl) fprintf(stderr,"asb-chromed: passwords write failed\n"); close(fd); }
  free(ct); }
static char g_pend_o[300], g_pend_u[200], g_pend_p[200]; static int g_pend_shown; static time_t g_pend_t;
static int pw_lookup(const char *db,const char *origin,char *user,int ucap,char *pass,int pcap,int *never){
  const char *l=db; *never=0; int found=0;   /* 0.78: read every row, so a "never" row after a saved login counts too */
  while(l && *l){ const char *e=strchr(l,'\n'); int len=e?(int)(e-l):(int)strlen(l);
    char row[800]; if(len>=(int)sizeof row) len=(int)sizeof row-1; memcpy(row,l,(size_t)len); row[len]=0;
    char *a=strchr(row,'\t'), *b=a?strchr(a+1,'\t'):NULL;
    if(a&&b){ *a=0; *b=0;
      if(!strcmp(row,"never") && !strcmp(a+1,origin)) *never=1;
      else if(!found && !strcmp(row,origin)){ if(user) scopy(user,(size_t)ucap,a+1); if(pass) scopy(pass,(size_t)pcap,b+1); found=1; } }
    explicit_bzero(row,sizeof row); l=e?e+1:NULL; }
  return found; }
static void origin_of(const char *url,char *o,int cap){
  const char *p=strstr(url,"://"); if(!p){ o[0]=0; return; }
  const char *s=strchr(p+3,'/'); int n=s?(int)(s-url):(int)strlen(url); if(n>=cap) n=cap-1;
  memcpy(o,url,(size_t)n); o[n]=0; }
static void pw_bar(void){
  if(!g_pend_o[0]) return;
  char eo[700], eu[500]; jesc(g_pend_o,eo,sizeof eo); jesc(g_pend_u[0]?g_pend_u:"(no user name)",eu,sizeof eu);
  int i=cdp_id();
  cdp("{\"id\":%d,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":\"(function(){var o=document.getElementById('asbpwbar');if(o)o.remove();"
      "var d=document.createElement('div');d.id='asbpwbar';d.style.cssText='position:fixed;z-index:2147483647;top:0;left:0;right:0;background:#378EF0;color:#fff;font:15px sans-serif;padding:10px 14px;box-shadow:0 2px 6px rgba(0,0,0,.4)';"
      "d.textContent='AmiSurf: save the password for %s at %s? ';"
      "function b(t){var x=document.createElement('button');x.type='button';x.textContent=t;x.style.cssText='margin-left:10px;font:15px sans-serif;padding:4px 14px;pointer-events:auto;cursor:pointer';d.appendChild(x)}"
      "b('Save');b('Never for this site');b('Not now');document.documentElement.appendChild(d)})()\"}}",i,eu,eo);
  g_pend_shown=1; }
/* 0.78: no page-side hooks. 0.77 put a capture script and a Runtime binding (asbPw) into every page; the binding only
   works with Runtime.enable, which pages can detect (anti-bot checks like Google's sign-in), and the bar's buttons
   relied on the page delivering their click events. Now the host asks the page, right before it forwards a left click
   or Enter, (1) whether the point is on one of the bar's buttons and (2) whether this submits a filled-in password
   form. Runtime.evaluate works without Runtime.enable and runs before the input event. k: 1 click at x,y, 2 Enter. */
static void pw_probe(int x,int y,int k){
  int i=cdp_id(); req_add(i,K_PWPROBE,NULL);
  cdp("{\"id\":%d,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":\"(function(x,y,k){try{"
      "var d=document.getElementById('asbpwbar');"
      "if(d&&k==1){var bs=d.querySelectorAll('button'),c=['save','never','no'],r;"
        "for(var i=0;i<bs.length&&i<3;i++){r=bs[i].getBoundingClientRect();if(x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom){setTimeout(function(){d.remove()},200);return 'cmd:'+c[i]}}"
        "r=d.getBoundingClientRect();if(y>=r.top&&y<r.bottom)return ''}"
      "var doc=document,e=null;"
      "for(var n=0;n<4;n++){e=k==1?doc.elementFromPoint(x,y):doc.activeElement;if(!e)return '';"
        "if(e.tagName=='IFRAME'||e.tagName=='FRAME'){var cd=null;try{cd=e.contentDocument}catch(z){}if(!cd)return '';var fr=e.getBoundingClientRect();x-=fr.left;y-=fr.top;doc=cd;continue}break}"
      "var f=null;"
      "if(k==1){var b=e.closest&&e.closest('button,input[type=submit],input[type=image],[role=button]');if(!b)return '';f=b.form||b.closest('form')}"
      "else{if(!/^(INPUT)$/.test(e.tagName))return '';f=e.form||e.closest('form')}"
      "var ps=(f||doc).querySelectorAll('input[type=password]'),p=null;for(var j=0;j<ps.length;j++)if(ps[j].value){p=ps[j];break}"
      "if(!p)return '';var s=(f||doc).querySelectorAll('input'),u=null;"
      "for(var m=0;m<s.length;m++){if(s[m]===p)break;var t=(s[m].type||'text').toLowerCase();if((t=='text'||t=='email'||t=='tel')&&s[m].value)u=s[m]}"
      "return JSON.stringify({o:doc.defaultView.location.origin,u:u?u.value:'',p:p.value})}catch(z){return ''}})(%d,%d,%d)\",\"returnByValue\":true}}",i,x,y,k); }
static void pw_autofill(void){
  char o[300]; origin_of(g_url,o,sizeof o); if(!o[0]) return;
  char *db=pw_load(); if(!db) return;
  char u[200], p[200]; int never;
  if(pw_lookup(db,o,u,sizeof u,p,sizeof p,&never)){
    char eu[500], ep[500]; jesc(u,eu,sizeof eu); jesc(p,ep,sizeof ep);
    int i=cdp_id();
    cdp("{\"id\":%d,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":\"(function(){var p=document.querySelector('input[type=password]');if(!p||p.value)return;"
        "var set=Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set;function f(e,v){set.call(e,v);e.dispatchEvent(new Event('input',{bubbles:true}));e.dispatchEvent(new Event('change',{bubbles:true}))}"
        "var ins=document.querySelectorAll('input'),u=null;for(var i=0;i<ins.length;i++){if(ins[i]===p)break;var t=(ins[i].type||'text').toLowerCase();if(t=='text'||t=='email'||t=='tel')u=ins[i]}"
        "if(u&&!u.value)f(u,'%s');f(p,'%s')})()\"}}",i,eu,ep);
    send_status("Password filled in from the AmiStudio password store"); }
  explicit_bzero(p,sizeof p); free(db); }
static void pw_binding(const char *payload){
  if(!strncmp(payload,"cmd:",4)){
    if(!g_pend_o[0]) return;
    char *db=pw_load(); if(!db) return;
    size_t cap=strlen(db)+1200; char *nd=malloc(cap); if(!nd){ free(db); return; }
    nd[0]=0; const char *l=db;
    while(l && *l){ const char *e=strchr(l,'\n'); int len=e?(int)(e-l+1):(int)strlen(l);
      char row[800]; int rl=len<(int)sizeof row-1?len:(int)sizeof row-1; memcpy(row,l,(size_t)rl); row[rl]=0;
      char key[600]; snprintf(key,sizeof key,"%s\t%s\t",g_pend_o,g_pend_u);
      if(strncmp(row,key,strlen(key))) strncat(nd,row,cap-strlen(nd)-1);
      l=e?e+1:NULL; }
    if(!strcmp(payload+4,"save")){ char r[800]; snprintf(r,sizeof r,"%s\t%s\t%s\n",g_pend_o,g_pend_u,g_pend_p); strncat(nd,r,cap-strlen(nd)-1); send_status("Password saved (encrypted)"); }
    else if(!strcmp(payload+4,"never")){ char r[400]; snprintf(r,sizeof r,"never\t%s\t\n",g_pend_o); strncat(nd,r,cap-strlen(nd)-1); send_status("Passwords will not be saved for this site"); }
    if(strcmp(payload+4,"no")) pw_save(nd);
    explicit_bzero(nd,strlen(nd)); explicit_bzero(db,strlen(db)); free(nd); free(db);
    explicit_bzero(g_pend_p,sizeof g_pend_p); g_pend_o[0]=g_pend_u[0]=0; return; }
  char o[300], u[200], p[200];
  if(!json_str(payload,"o",o,sizeof o)||!json_str(payload,"p",p,sizeof p)||!p[0]) return;
  json_str(payload,"u",u,sizeof u);
  char *db=pw_load(); if(!db) return;
  char su[200], sp[200]; int never;
  int have=pw_lookup(db,o,su,sizeof su,sp,sizeof sp,&never);
  free(db);
  if(never || (have && !strcmp(su,u) && !strcmp(sp,p))) return;
  scopy(g_pend_o,sizeof g_pend_o,o); scopy(g_pend_u,sizeof g_pend_u,u); scopy(g_pend_p,sizeof g_pend_p,p);
  g_pend_shown=0; g_pend_t=time(NULL); pw_bar(); explicit_bzero(p,sizeof p); }

/* ---- zoom ---- */
static void send_zoom(void){ if(g_cli<0) return; unsigned char b[2]; asbc_put_u16(b,(unsigned)g_zoom); asbc_send(g_cli,ASBC_ZOOM,b,2); }
static void set_zoom(int z){
  if(z<25) z=25; if(z>400) z=400; g_zoom=z;
  cdp_viewport(g_vw,g_vh);
  if(g_screencast_on){ cdp_screencast(0); cdp_screencast(1); }
  free(g_prev); g_prev=NULL; g_prev_w=g_prev_h=0;
  char s[40]; snprintf(s,sizeof s,"Zoom %d%%",z); send_status(s); send_zoom(); }
static int zoom_step(int dir){
  static const int st[]={25,33,50,67,75,80,90,100,110,125,150,175,200,250,300,400};
  int n=(int)(sizeof st/sizeof st[0]);
  if(dir>0){ for(int i=0;i<n;i++) if(st[i]>g_zoom) return st[i]; return 400; }
  for(int i=n-1;i>=0;i--) if(st[i]<g_zoom) return st[i]; return 25; }

/* ---- find with count ---- */
static void send_findres(void){ if(g_cli<0) return; unsigned char b[4]; asbc_put_u16(b,(unsigned)g_findcur); asbc_put_u16(b+2,(unsigned)g_findtot); asbc_send(g_cli,ASBC_FINDRES,b,4);
  char s[80]; if(g_findtot) snprintf(s,sizeof s,"Find: %d of %d",g_findcur,g_findtot); else snprintf(s,sizeof s,"Find: no matches"); send_status(s); }
static void find_run(int dir, int fresh){
  if(!g_findq[0]) return;
  char esc[900]; jesc(g_findq,esc,sizeof esc);
  int i;
  if(fresh){
    i=cdp_id(); req_add(i,K_FINDCOUNT,NULL);
    cdp("{\"id\":%d,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":\"(function(q){var t=document.body?document.body.innerText.toLowerCase():'';q=q.toLowerCase();var n=0,k=0;if(!q)return 0;while((k=t.indexOf(q,k))>=0){n++;k+=q.length}getSelection().removeAllRanges();window.find(q,false,false,true);return n})('%s')\",\"returnByValue\":true}}",i,esc);
    return; }
  i=cdp_id();
  cdp("{\"id\":%d,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":\"window.find('%s',false,%s,true)\"}}",i,esc,dir<0?"true":"false");
  if(g_findtot){ g_findcur+=dir; if(g_findcur>g_findtot) g_findcur=1; if(g_findcur<1) g_findcur=g_findtot; }
  send_findres(); }

/* ---- tabs ---- */
static void tab_new(const char *url){
  if(g_bidi){ int t=bidi_tab_create(url&&url[0]?url:NULL); if(t<0) t=g_ntab-1; attach_tab(t); if(g_defzoom!=100) set_zoom(g_zoom); return; }
  buf_t r={0}; char path[2600], enc[2500];
  if(url&&url[0]){ uenc(url,enc,sizeof enc); snprintf(path,sizeof path,"/json/new?%s",enc); } else snprintf(path,sizeof path,"/json/new?about:blank");
  if(http_verb(g_dbg,"PUT",path,&r) && http_get(g_dbg,path,&r)) fprintf(stderr,"asb-chromed: tab new failed\n");
  char nid[80]=""; if(r.p) json_str(r.p,"id",nid,sizeof nid); scopy(g_tab_mine,sizeof g_tab_mine,nid);
  free(r.p); usleep(150000); load_tabs();
  { int t=tab_by_id(nid); attach_tab(t>=0?t:g_ntab-1); }
  if(g_defzoom!=100) set_zoom(g_zoom); }
static void closed_push(const char *u){
  if(!u[0]||!strcmp(u,"about:blank")) return;
  if(g_nclosed==16){ memmove(g_closed[0],g_closed[1],sizeof g_closed[0]*15); g_nclosed=15; }
  scopy(g_closed[g_nclosed++],sizeof g_closed[0],u); }

/* ---- internal pages (HTTP on 127.0.0.1:g_pageport) ---- */
static void page_css(buf_t *o);
static void bput(buf_t *b,const char *fmt,...){
  va_list ap; va_start(ap,fmt); char tmp[8192]; int n=vsnprintf(tmp,sizeof tmp,fmt,ap); va_end(ap);
  if(n<0) return; if(n>=(int)sizeof tmp) n=(int)sizeof tmp-1;
  buf_need(b,b->n+n+1); memcpy(b->p+b->n,tmp,(size_t)n); b->n+=n; b->p[b->n]=0; }
static void page_css(buf_t *o){
  bput(o,"<!doctype html><html><head><meta charset=utf-8><style>body{font:16px sans-serif;background:#282828;color:#eee;margin:0}"
         "header{background:#535353;padding:12px 20px;font-size:22px}header a{color:#ccc;font-size:15px;margin-left:18px}"
         "main{padding:16px 20px}a{color:#7cb7ff}table{border-collapse:collapse;width:100%%}td{padding:6px 8px;border-bottom:1px solid #3a3a3a;vertical-align:top}"
         "input,select{font:16px sans-serif;background:#454545;color:#eee;border:1px solid #666;padding:5px}button{font:16px sans-serif;padding:5px 14px;background:#378EF0;color:#fff;border:0}"
         ".dim{color:#999;font-size:13px}.bar{height:8px;background:#454545;width:240px}.bar i{display:block;height:8px;background:#378EF0}</style>");
}
static void page_head(buf_t *o,const char *title){
  page_css(o);
  bput(o,"<title>%s</title></head><body><header>%s<a href=/bookmarks>Bookmarks</a><a href=/history>History</a><a href=/downloads>Downloads</a><a href=/passwords>Passwords</a><a href=/settings>Settings</a></header><main>",title,title); }
static void page_list(buf_t *o,const char *file,const char *q,const char *self,int editable){
  static ent_t e[LMAX]; int n=list_read(file,e,LMAX);
  char eq[400]; hesc(q,eq,sizeof eq);
  bput(o,"<form action=/%s><input name=q value=\"%s\" placeholder=Search size=40 autofocus> <button>Search</button></form><p></p><table>",self,eq);
  int shown=0;
  for(int k=0;k<n && shown<400;k++){ int i=editable?k:n-1-k;
    if(!ci_has(e[i].url,q) && !ci_has(e[i].title,q)) continue; shown++;
    char hu[2600], ht[500], tm[40]; hesc(e[i].url,hu,sizeof hu); hesc(e[i].title[0]?e[i].title:e[i].url,ht,sizeof ht);
    time_t tt=(time_t)e[i].t; strftime(tm,sizeof tm,"%Y-%m-%d %H:%M",localtime(&tt));
    char hf[200]=""; if(e[i].folder[0]){ char t[150]; hesc(e[i].folder,t,sizeof t); snprintf(hf,sizeof hf,"[%s] ",t); }
    bput(o,"<tr><td><a href=\"%s\">%s%s</a><div class=dim>%s</div></td><td class=dim>%s</td><td>",hu,hf,ht,hu,tm);
    if(editable) bput(o,"<a href=\"/%s?edit=%d\">Edit</a> &nbsp; ",self,i);
    bput(o,"<a href=\"/%s?del=%d&k=%s&q=%s\">Delete</a></td></tr>",self,i,g_pagekey,eq);
  }
  bput(o,"</table>%s",shown?"":"<p class=dim>Nothing here yet.</p>");
  if(!editable && n) bput(o,"<p><a href=\"/%s?clear=1&k=%s\">Clear all history</a></p>",self,g_pagekey); }
static void page_serve(int c){
  struct timeval to={1,0}; setsockopt(c,SOL_SOCKET,SO_RCVTIMEO,&to,sizeof to);
  char req[8192]; int n=0;
  while(n<(int)sizeof req-1){ ssize_t k=read(c,req+n,sizeof req-1-(size_t)n); if(k<=0) break; n+=(int)k; req[n]=0; if(strstr(req,"\r\n\r\n")) break; }
  req[n]=0;
  if(n==0){ close(c); return; }   /* 0.87: an idle (preconnected) socket gets no reply; a 403 written to it was shown as the next page */
  char host[80]=""; { const char *h=strcasestr(req,"\r\nHost:"); if(h){ h+=7; while(*h==' ') h++; int i=0; while(*h&&*h!='\r'&&i<79) host[i++]=*h++; host[i]=0; } }
  char want[80]; snprintf(want,sizeof want,"127.0.0.1:%d",g_pageport);
  buf_t o={0}; const char *status="200 OK";
  if(strncmp(req,"GET ",4) || strcmp(host,want)){ status="403 Forbidden"; bput(&o,"forbidden"); goto send; } /* Host check stops DNS rebinding */
  char path[4096]; { const char *p=req+4; int i=0; while(*p&&*p!=' '&&i<(int)sizeof path-1) path[i++]=*p++; path[i]=0; }
  char *q=strchr(path,'?'); if(q) *q++=0; else q="";
  char k[40], v[1300], v2[1300]; qparam(q,"k",k,sizeof k); int authed=!strcmp(k,g_pagekey);
  if(!strcmp(path,"/bookmarks")||!strcmp(path,"/history")){
    int bm=!strcmp(path,"/bookmarks"); const char *file=bm?"bookmarks.tsv":"history.tsv";
    if(authed && qparam(q,"del",v,sizeof v)){ static ent_t e[LMAX]; int cnt=list_read(file,e,LMAX), d=atoi(v);
      if(d>=0&&d<cnt){ memmove(e+d,e+d+1,sizeof(ent_t)*(size_t)(cnt-d-1)); list_write(file,e,cnt-1); if(bm) send_bmarks(); } }
    if(authed && !bm && qparam(q,"clear",v,sizeof v)) list_write(file,NULL,0);
    if(authed && bm && qparam(q,"save",v,sizeof v)){ static ent_t e[LMAX]; int cnt=list_read(file,e,LMAX), d=atoi(v);
      char fo[200]=""; qparam(q,"url",v,sizeof v); qparam(q,"title",v2,sizeof v2); qparam(q,"folder",fo,sizeof fo); clean_field(v); clean_field(v2); clean_field(fo);
      if(d>=0&&d<cnt&&v[0]){ scopy(e[d].url,sizeof e[d].url,v); scopy(e[d].title,sizeof e[d].title,v2); scopy(e[d].folder,sizeof e[d].folder,fo); list_write(file,e,cnt); send_bmarks(); } }
    page_head(&o,bm?"Bookmarks":"History");
    if(bm && qparam(q,"edit",v,sizeof v)){ static ent_t e[LMAX]; int cnt=list_read(file,e,LMAX), d=atoi(v);
      if(d>=0&&d<cnt){ char hu[2600], ht[500], hf[200]; hesc(e[d].url,hu,sizeof hu); hesc(e[d].title,ht,sizeof ht); hesc(e[d].folder,hf,sizeof hf);
        bput(&o,"<form action=/bookmarks><input type=hidden name=k value=%s><input type=hidden name=save value=%d>"
                "<p>Title<br><input name=title size=60 value=\"%s\"></p><p>Address<br><input name=url size=80 value=\"%s\"></p>"
                "<p>Folder <span class=dim>(empty: on the bookmarks bar itself; a name: in that folder)</span><br><input name=folder size=30 value=\"%s\"></p>"
                "<button>Save</button> <a href=/bookmarks>Cancel</a></form><hr>",g_pagekey,d,ht,hu,hf); } }
    qparam(q,"q",v,sizeof v); page_list(&o,file,v,bm?"bookmarks":"history",bm);
  } else if(!strcmp(path,"/downloads")){
    int running=0; for(int i=0;i<g_ndl;i++) if(g_dl[i].state==0) running=1;
    page_css(&o); if(running) bput(&o,"<meta http-equiv=refresh content=2>");
    bput(&o,"<title>Downloads</title></head><body><header>Downloads<a href=/bookmarks>Bookmarks</a><a href=/history>History</a><a href=/passwords>Passwords</a><a href=/settings>Settings</a></header><main><p class=dim>Saved to %s</p><table>",dl_where());
    for(int i=g_ndl-1;i>=0;i--){ dl_t *d=&g_dl[i]; char hn[600]; hesc(d->name,hn,sizeof hn);
      int pc=d->total>0?(int)(d->got*100/d->total):0;
      bput(&o,"<tr><td>%s<div class=dim>%.1f of %.1f MB</div></td><td>",hn,d->got/1048576.0,d->total/1048576.0);
      if(d->state==0) bput(&o,"<div class=bar><i style=width:%d%%></i></div>%d%%",pc,pc); else bput(&o,"%s",d->state==1?"done":"failed or canceled");
      bput(&o,"</td></tr>"); }
    static ent_t e[LMAX]; int cnt=list_read("downloads.tsv",e,LMAX);
    for(int i=cnt-1;i>=0 && i>=cnt-200;i--){ char hn[500], hu[2600], tm[40]; hesc(e[i].title,hn,sizeof hn); hesc(e[i].url,hu,sizeof hu);
      time_t tt=(time_t)e[i].t; strftime(tm,sizeof tm,"%Y-%m-%d %H:%M",localtime(&tt));
      bput(&o,"<tr><td>%s<div class=dim>%s</div></td><td class=dim>%s</td></tr>",hn,hu,tm); }
    bput(&o,"</table>%s",(g_ndl||cnt)?"":"<p class=dim>No downloads yet.</p>");
  } else if(!strcmp(path,"/settings")){
    int saved=0;
    if(authed && qparam(q,"save",v,sizeof v)){
      if(qparam(q,"home",v,sizeof v) && v[0]){   /* "example.org" -> https://example.org (a bare name is not a URL Chrome can open) */
        if(!strstr(v,"://") && strncmp(v,"about:",6) && strncmp(v,"asb:",4) && strncmp(v,"chrome:",7) && strncmp(v,"data:",5)) snprintf(g_homepage,sizeof g_homepage,"https://%.1180s",v);
        else scopy(g_homepage,sizeof g_homepage,v); }
      if(qparam(q,"search",v,sizeof v) && strstr(v,"%s")) scopy(g_search,sizeof g_search,v);
      if(qparam(q,"zoom",v,sizeof v)){ int z=atoi(v); if(z>=25&&z<=400) g_defzoom=z; }
      saved=settings_save(); fprintf(stderr,"asb-chromed: settings %s (home %s)\n",saved>0?"saved":"NOT saved",g_homepage); }
    else if(qparam(q,"save",v,sizeof v)) saved=-2;   /* stale page key (the engine restarted since the page was opened) */
    char hh[2600], hs[1300]; hesc(g_homepage,hh,sizeof hh); hesc(g_search,hs,sizeof hs);
    page_head(&o,"Settings");
    /* 0.81: say whether it worked, and take the one-shot ?save=... query out of the address (beta report: "can't be saved") */
    if(saved>0) bput(&o,"<p style=\"background:#1d4d2b;color:#fff;padding:6px\">Saved. The Home button opens this home page (unless a home page is set in the Amiga Settings window).</p>");
    else if(saved==-1) bput(&o,"<p style=\"background:#6b1f1f;color:#fff;padding:6px\">Could not write %s/settings.</p>",g_cfg);
    else if(saved==-2) bput(&o,"<p style=\"background:#6b1f1f;color:#fff;padding:6px\">Not saved: this page is from before the engine restarted. Change the values below and Save again.</p>");
    if(saved) bput(&o,"<script>history.replaceState(null,'','/settings')</script>");
    bput(&o,"<form action=/settings><input type=hidden name=k value=%s><input type=hidden name=save value=1>"
            "<p>Home page<br><input name=home size=70 value=\"%s\"></p>"
            "<p>Search engine (%%s = query)<br><input name=search size=70 value=\"%s\"></p>"
            "<p>Default zoom<br><input name=zoom size=5 value=%d> %%</p><button>Save</button></form>"
            "<p class=dim>These are host settings (saved in %s/settings). The Amiga's Home button uses this home page unless one is set in the client's Settings window. Amiga screen, theme, font and connection (host, port) settings are in the client's Settings window (menu: Settings...).</p>",
            g_pagekey,hh,hs,g_defzoom,g_cfg);
  } else if(!strcmp(path,"/passwords")){
    char *db=pw_load();
    if(db && authed && qparam(q,"del",v,sizeof v)){ int d=atoi(v), row=0; size_t cap=strlen(db)+1; char *nd=calloc(1,cap); const char *l=db;
      while(nd && l && *l){ const char *e=strchr(l,'\n'); int len=e?(int)(e-l+1):(int)strlen(l); if(row!=d){ size_t nl=strlen(nd); memcpy(nd+nl,l,(size_t)len); nd[nl+(size_t)len]=0; } row++; l=e?e+1:NULL; }
      if(nd){ pw_save(nd); explicit_bzero(nd,strlen(nd)); free(nd); } explicit_bzero(db,strlen(db)); free(db); db=pw_load(); }
    page_head(&o,"Passwords");
    bput(&o,"<p class=dim>Stored encrypted (AES-256-GCM) in %s/passwords.dat; the key is in key.bin next to it (mode 0600). Passwords are never shown here.</p><table>",g_cfg);
    int row=0, any=0; const char *l=db?db:"";
    while(*l){ const char *e=strchr(l,'\n'); int len=e?(int)(e-l):(int)strlen(l); char r[800]; if(len>=(int)sizeof r) len=(int)sizeof r-1; memcpy(r,l,(size_t)len); r[len]=0;
      char *a=strchr(r,'\t'), *b=a?strchr(a+1,'\t'):NULL;
      if(a&&b){ *a=0; *b=0; char h1[700], h2[500]; any=1;
        if(!strcmp(r,"never")){ hesc(a+1,h1,sizeof h1); bput(&o,"<tr><td>%s</td><td class=dim>never save</td>",h1); }
        else { hesc(r,h1,sizeof h1); hesc(a+1,h2,sizeof h2); bput(&o,"<tr><td>%s</td><td>%s &nbsp; <span class=dim>&bull;&bull;&bull;&bull;&bull;&bull;</span></td>",h1,h2); }
        bput(&o,"<td><a href=\"/passwords?del=%d&k=%s\">Delete</a></td></tr>",row,g_pagekey); }
      row++; if(!e) break; l=e+1; }
    bput(&o,"</table>%s",any?"":"<p class=dim>No saved passwords. Log in to a site and choose Save on the blue bar.</p>");
    if(db){ explicit_bzero(db,strlen(db)); free(db); }
  } else { page_head(&o,"AmiSurf"); bput(&o,"<p><a href=/bookmarks>Bookmarks</a> &middot; <a href=/history>History</a> &middot; <a href=/downloads>Downloads</a> &middot; <a href=/passwords>Passwords</a> &middot; <a href=/settings>Settings</a></p>"); }
  bput(&o,"</main></body></html>");
send:;
  char hdr[300]; int hl=snprintf(hdr,sizeof hdr,"HTTP/1.1 %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %d\r\nX-Frame-Options: DENY\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",status,o.n);
  if(wr_all(c,hdr,(size_t)hl)==0 && o.n) wr_all(c,o.p,(size_t)o.n);
  free(o.p); close(c); }

/* asb://bookmarks etc. and "?query" searches */
static int nav_rewrite(const char *in, char *out, int cap){
  if(!strncmp(in,"asb://",6)||!strncmp(in,"asb:",4)){ const char *p=in+(in[4]=='/'?6:4); snprintf(out,(size_t)cap,"http://127.0.0.1:%d/%s",g_pageport,p); return 1; }
  if(in[0]=='?'){ char enc[1500]; uenc(in+1,enc,sizeof enc); char *f=strstr(g_search,"%s");
    if(f) snprintf(out,(size_t)cap,"%.*s%s%s",(int)(f-g_search),g_search,enc,f+2); else snprintf(out,(size_t)cap,"%s",g_search); return 1; }
  return 0; }
static void display_url(const char *in,char *out,int cap){ /* show internal pages as asb://name */
  if(is_internal(in)){ char pre[64]; int n=snprintf(pre,sizeof pre,"http://127.0.0.1:%d/",g_pageport); snprintf(out,(size_t)cap,"asb://%s",in+n); }
  else scopy(out,(size_t)cap,in); }

/* ---- save page ---- */
static void save_b64_reply(const char *msg,const char *name,const char *what){
  const char *p=strstr(msg,"\"data\":\""); if(!p){ send_status("Save failed"); return; }
  p+=8; const char *e=strchr(p,'"'); if(!e) return;
  int cap=(int)((e-p)*3/4+4); unsigned char *b=malloc((size_t)cap); if(!b) return;
  int n=b64dec(p,(int)(e-p),b,cap);
  char path[1200]; unique_path(path,sizeof path,g_dlpath,name);
  FILE *f=fopen(path,"wb"); if(f){ fwrite(b,1,(size_t)n,f); fclose(f); }
  free(b);
  char s[1300]; snprintf(s,sizeof s,"Saved %s: %s",what,strrchr(path,'/')+1); send_status(s);
  list_append("downloads.tsv",g_url,strrchr(path,'/')+1); }
static void title_name(char *o,int cap,const char *ext){ safe_name(g_title,o,cap,ext); }

/* ---- handshake failure limiting (non-loopback only) ---- */
static struct { uint32_t ip; int fails; time_t first, until; } g_ban[64];
static int ban_check(uint32_t ip){
  if((ntohl(ip)>>24)==127) return 0;
  time_t now=time(NULL);
  for(int i=0;i<64;i++) if(g_ban[i].ip==ip && g_ban[i].until>now) return 1;
  return 0; }
static void ban_fail(uint32_t ip){
  if((ntohl(ip)>>24)==127) return;
  time_t now=time(NULL); int slot=-1, old=0;
  for(int i=0;i<64;i++){ if(g_ban[i].ip==ip){ slot=i; break; } if(g_ban[i].first<g_ban[old].first) old=i; }
  if(slot<0){ slot=old; g_ban[slot].ip=ip; g_ban[slot].fails=0; g_ban[slot].first=now; g_ban[slot].until=0; }
  if(now-g_ban[slot].first>60){ g_ban[slot].fails=0; g_ban[slot].first=now; }
  if(++g_ban[slot].fails>=5){ g_ban[slot].until=now+300; struct in_addr a; a.s_addr=ip; fprintf(stderr,"asb-chromed: %s blocked for 5 minutes after %d failed handshakes\n",inet_ntoa(a),g_ban[slot].fails); } }

/* ---- client commands ---- */
static void do_cmd(char *c){
  char *arg=strchr(c,' '); if(arg) *arg++=0; else arg="";
  int i;
  if(!strcmp(c,"zoom")){ if(!strcmp(arg,"+")) set_zoom(zoom_step(1)); else if(!strcmp(arg,"-")) set_zoom(zoom_step(-1)); else if(!strcmp(arg,"0")) set_zoom(100); else set_zoom(atoi(arg)); }
  else if(!strcmp(c,"findnext")) find_run(1,0);
  else if(!strcmp(c,"findprev")) find_run(-1,0);
  else if(!strcmp(c,"findclose")){ g_findq[0]=0; g_findcur=g_findtot=0; i=cdp_id(); cdp("{\"id\":%d,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":\"getSelection().removeAllRanges()\"}}",i); }
  else if(!strcmp(c,"ctx")){ int x=0,y=0; sscanf(arg,"%d %d",&x,&y); x=x*100/g_zoom; y=y*100/g_zoom;
    i=cdp_id(); req_add(i,K_CTX,arg);
    cdp("{\"id\":%d,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":\"(function(x,y){var e=document.elementFromPoint(x,y),a=e&&e.closest?e.closest('a[href]'):null,m=e&&e.closest?e.closest('img'):null;return (a?a.href:'')+'\\\\n'+(m?(m.currentSrc||m.src):'')+'\\\\n'+String(getSelection()).slice(0,2000)})(%d,%d)\",\"returnByValue\":true}}",i,x,y); }
  else if(!strcmp(c,"newtab")){ char u[2400]; if(!nav_rewrite(arg,u,sizeof u)) scopy(u,sizeof u,arg); tab_new(u); }
  else if(!strcmp(c,"saveimg")){
    if(strncmp(arg,"http",4)){ send_status("Cannot save this image"); return; }
    const char *b=strrchr(arg,'/'); char nm[300]; char tmp[300]; scopy(tmp,sizeof tmp,b?b+1:"image"); char *qq=strchr(tmp,'?'); if(qq) *qq=0;
    safe_name(tmp[0]?tmp:"image",nm,sizeof nm,NULL); if(!strchr(nm,'.')) strncat(nm,".img",sizeof nm-strlen(nm)-1);
    char path[1200]; unique_path(path,sizeof path,g_dlpath,nm);
    pid_t p=fork(); if(p==0){ int dn=open("/dev/null",O_RDWR); if(dn>=0){ dup2(dn,0); dup2(dn,1); dup2(dn,2); }
      execlp("curl","curl","-sL","--max-time","120","-A","Mozilla/5.0 AmiSurf","-e",g_url,"-o",path,arg,(char*)0); _exit(127); }
    char s[1300]; snprintf(s,sizeof s,"Saving image %s",strrchr(path,'/')+1); send_status(s);
    list_append("downloads.tsv",arg,strrchr(path,'/')+1); }
  else if(!strcmp(c,"savepng")){ char nm[300]; title_name(nm,sizeof nm,".png"); i=cdp_id(); req_add(i,K_SAVEPNG,nm);
    cdp("{\"id\":%d,\"method\":\"Page.captureScreenshot\",\"params\":{\"format\":\"png\"}}",i); }
  else if(!strcmp(c,"savepdf")){ char nm[300]; title_name(nm,sizeof nm,".pdf"); i=cdp_id(); req_add(i,K_SAVEPDF,nm);
    cdp("{\"id\":%d,\"method\":\"Page.printToPDF\",\"params\":{\"printBackground\":true}}",i); send_status("Printing to PDF..."); }
  else if(!strcmp(c,"savehtml")){ char nm[300]; title_name(nm,sizeof nm,".html"); i=cdp_id(); req_add(i,K_SAVEHTML,nm);
    cdp("{\"id\":%d,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":\"'<!doctype html>\\\\n'+document.documentElement.outerHTML\",\"returnByValue\":true}}",i); }
  else if(!strcmp(c,"source")){ if(!strncmp(g_url,"view-source:",12)) return; char u[2500]; snprintf(u,sizeof u,"view-source:%s",g_url); tab_new(u); }
  else if(!strcmp(c,"reopen")){ if(!g_nclosed){ send_status("No closed tabs"); return; } char u[2400]; scopy(u,sizeof u,g_closed[--g_nclosed]); tab_new(u); }
  else if(!strcmp(c,"tabmove")){ int f=-1,t=-1; sscanf(arg,"%d %d",&f,&t); load_tabs();
    if(f<0||t<0||f>=g_ntab||t>=g_ntab||f==t) return;
    char cur[80]; scopy(cur,sizeof cur,g_tabs[g_tab].id);
    char mv[80]; scopy(mv,sizeof mv,g_order[f]);
    if(f<t) memmove(g_order[f],g_order[f+1],sizeof g_order[0]*(size_t)(t-f)); else memmove(g_order[t+1],g_order[t],sizeof g_order[0]*(size_t)(f-t));
    scopy(g_order[t],sizeof g_order[0],mv);
    load_tabs(); { int k=tab_by_id(cur); if(k>=0) g_tab=k; } send_tabs(); }
  else if(!strcmp(c,"tabnext")||!strcmp(c,"tabprev")){ load_tabs(); if(g_ntab<2) return; int t=(g_tab+(c[3]=='n'?1:g_ntab-1))%g_ntab; attach_tab(t); }
  else if(!strcmp(c,"bookmark")) bookmark_current();
  else if(!strcmp(c,"bookmarks")){ g_bm_want=1; send_bmarks(); }
  else if(!strcmp(c,"suggest")) suggest(arg);
  else if(!strcmp(c,"home")) cdp_nav(g_homepage);
  else if(!strcmp(c,"avdelay")){ int d=atoi(arg); if(d<0) d=0; if(d>1500) d=1500; if(d!=g_avdelay_ms) fprintf(stderr,"asb-chromed: picture held back %d ms for A/V sync\n",d); g_avdelay_ms=d; }
  else if(!strcmp(c,"page")){ char u[200]; snprintf(u,sizeof u,"http://127.0.0.1:%d/%s",g_pageport,arg); cdp_nav(u); }
  else fprintf(stderr,"asb-chromed: unknown cmd %s\n",c);
}

/* replies to requests above; returns 1 if consumed */
static int feat_reply(const char *msg){
  int rid=json_int(msg,"id",0); if(rid<=0) return 0;
  int k=-1; for(int i=0;i<32;i++) if(g_req[i].id==rid){ k=i; break; }
  if(k<0) return 0;
  int kind=g_req[k].kind; char arg[160]; scopy(arg,sizeof arg,g_req[k].arg); g_req[k].id=0;
  if(kind==K_PWPROBE){ int cap=(int)strlen(msg)+1; char *v=malloc((size_t)cap); if(!v) return 1; v[0]=0;
    if(!strstr(msg,"\"error\"") && json_str(msg,"value",v,cap) && v[0]) pw_binding(v);
    explicit_bzero(v,(size_t)cap); free(v); return 1; }
  if(strstr(msg,"\"error\"") && kind!=K_CTX){ char em[200]=""; json_str(msg,"message",em,sizeof em); char s[260]; snprintf(s,sizeof s,"Failed: %s",em); send_status(s); return 1; }
  if(kind==K_FINDCOUNT){ g_findtot=json_int(msg,"value",0); g_findcur=g_findtot?1:0; send_findres(); }
  else if(kind==K_CTX){ char v[5000]=""; json_str(msg,"value",v,sizeof v); if(g_cli>=0) asbc_send(g_cli,ASBC_CTX,v,(uint32_t)strlen(v)); }
  else if(kind==K_SAVEPNG) save_b64_reply(msg,arg,"screenshot");
  else if(kind==K_SAVEPDF) save_b64_reply(msg,arg,"PDF");
  else if(kind==K_SAVEHTML){ int cap=(int)strlen(msg); char *v=malloc((size_t)cap+1); if(!v) return 1; json_str(msg,"value",v,cap);
    char path[1200]; unique_path(path,sizeof path,g_dlpath,arg); FILE *f=fopen(path,"w"); if(f){ fputs(v,f); fclose(f); } free(v);
    char s[1300]; snprintf(s,sizeof s,"Saved HTML: %s",strrchr(path,'/')+1); send_status(s); list_append("downloads.tsv",g_url,strrchr(path,'/')+1); }
  return 1; }

/* CDP events for downloads and passwords; returns 1 if consumed */
static int feat_event(const char *method,const char *msg){
  if(!strcmp(method,"Browser.downloadWillBegin")){
    char fn[256]="", gu[64]=""; if(!json_str(msg,"suggestedFilename",fn,sizeof fn)) scopy(fn,sizeof fn,"file"); json_str(msg,"guid",gu,sizeof gu);
    /* 0.85: with the browser connection open the page session may deliver the same event too: once per guid */
    if(gu[0] && dl_find(gu)) return 1;
    if(g_ndl==32){ memmove(g_dl,g_dl+1,sizeof g_dl[0]*31); g_ndl=31; }
    dl_t *d=&g_dl[g_ndl++]; memset(d,0,sizeof *d); scopy(d->guid,sizeof d->guid,gu); scopy(d->name,sizeof d->name,fn); json_str(msg,"url",d->url,sizeof d->url); d->t=time(NULL);
    char st[900]; snprintf(st,sizeof st,"Downloading %s to %s",fn,dl_where()); send_status(st);
    fprintf(stderr,"asb-chromed: download %s -> %s\n",fn,g_dlpath); return 1; }
  if(!strcmp(method,"Browser.downloadProgress")){
    char gu[64]="", st[40]=""; json_str(msg,"guid",gu,sizeof gu); json_str(msg,"state",st,sizeof st);
    dl_t *d=dl_find(gu); if(!d || d->state) return 1;
    const char *p=strstr(msg,"\"receivedBytes\":"); if(p) d->got=atof(p+16);
    p=strstr(msg,"\"totalBytes\":"); if(p) d->total=atof(p+13);
    if(!strcmp(st,"completed")){ d->state=1; if(d->total<=0) d->total=d->got; dl_finish(d); char s[900]; snprintf(s,sizeof s,"Downloaded %s (%.0f KB) to %s",d->name,d->got/1024.0,dl_where()); send_status(s);
      fprintf(stderr,"asb-chromed: downloaded %s/%s (%.0f bytes)\n",g_dlpath,d->name,d->got); }
    else if(!strcmp(st,"canceled")){ d->state=2; dl_finish(d); char s[600];
      /* Chrome reports a failed download (network error, server error, disk full) as canceled too */
      snprintf(s,sizeof s,"Download failed or canceled: %s (%.0f KB received) - see the Downloads page",d->name,d->got/1024.0); send_status(s);
      fprintf(stderr,"asb-chromed: download failed/canceled %s\n",d->name); }
    else { static time_t last; time_t now=time(NULL); if(now!=last){ last=now; char s[400];
        if(d->total>0) snprintf(s,sizeof s,"Downloading %s  %d%%  (%.1f of %.1f MB)",d->name,(int)(d->got*100/d->total),d->got/1048576.0,d->total/1048576.0);
        else snprintf(s,sizeof s,"Downloading %s  %.1f MB",d->name,d->got/1048576.0);
        send_status(s); } }
    return 1; }
  if(!strcmp(method,"Runtime.bindingCalled")){
    char name[40]; json_str(msg,"name",name,sizeof name);
    if(!strcmp(name,"asbPw")){ char pl[1200]; json_str(msg,"payload",pl,sizeof pl); pw_binding(pl); explicit_bzero(pl,sizeof pl); }
    return 1; }
  if(!strcmp(method,"Page.loadEventFired")){
    if(g_pend_o[0]){ if(time(NULL)-g_pend_t<120) pw_bar(); else { explicit_bzero(g_pend_p,sizeof g_pend_p); g_pend_o[0]=g_pend_u[0]=0; } }
    pw_autofill(); return 0; }
  return 0; }

static void feat_enable(void){ /* per attached tab: 0.78 injects nothing into pages (see pw_probe) */ }

static void feat_init(int asbcport){
  const char *e=getenv("ASB_CONFIG"); const char *home=getenv("HOME"); if(!home) home="/tmp";
  if(e&&e[0]) scopy(g_cfg,sizeof g_cfg,e);
  else { struct stat st; char old[620]; snprintf(g_cfg,sizeof g_cfg,"%s/.config/amisurf",home); snprintf(old,sizeof old,"%s/.config/amistudio-browser",home);
    if(stat(g_cfg,&st) && !stat(old,&st) && S_ISDIR(st.st_mode)){ scopy(g_cfg,sizeof g_cfg,old); fprintf(stderr,"asb-chromed: using the old data folder %s (AmiStudio Browser); rename it to ~/.config/amisurf when you no longer need 0.81\n",g_cfg); } }
  { char p[620]; snprintf(p,sizeof p,"%s/.config",home); mkdir(p,0755); } mkdir(g_cfg,0700);
  settings_load(); g_zoom=g_defzoom;
  unsigned char r[16]; if(RAND_bytes(r,16)!=1){ for(int i=0;i<16;i++) r[i]=(unsigned char)rng32(); }
  for(int i=0;i<16;i++) snprintf(g_pagekey+i*2,3,"%02x",r[i]);
  g_pageport=asbcport+1;
  g_pagefd=tcp_listen_local(g_pageport,"127.0.0.1"); /* always loopback: only Chrome on this host opens these pages */
  if(g_pagefd<0) fprintf(stderr,"asb-chromed: pages port %d busy; asb:// pages disabled\n",g_pageport);
  else fprintf(stderr,"asb-chromed: pages on 127.0.0.1:%d, config %s\n",g_pageport,g_cfg); }
