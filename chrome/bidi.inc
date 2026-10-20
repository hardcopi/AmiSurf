/* asb-chromed Firefox backend (0.71): WebDriver BiDi over Firefox's --remote-debugging-port.
   The rest of the host speaks CDP; with g_bidi set, cdp() hands each command to bidi_from_cdp(), which
   rewrites it as the matching BiDi command, and on_bidi() turns BiDi replies/events back into the CDP
   shapes on_cdp() already understands. BiDi has no screencast, so frames come from polling
   browsingContext.captureScreenshot (one in flight; the next is due as soon as the client keeps up).
   Included from host.c after the WebSocket helpers. */

static int g_cast_id; static int g_cast_ms=40; /* g_bidi, g_ctx, g_cast_due: host.c */
static int g_tabs_dirty;
static unsigned g_pre_hash[16]; static int g_npre; /* preload scripts already added (they are global) */
static int g_bidi_dl;                 /* download behaviour set */
static void on_cdp(const char *msg);
static long long now_ms(void);
static void frame_png(int n);
static void ensure_xvfb(void);

/* copy the raw JSON value of "key" (string with its quotes and escapes, number, object, array) */
static int json_raw(const char *j, const char *key, char *out, int cap){
  char pat[80]; snprintf(pat,sizeof pat,"\"%s\"",key);
  const char *p=strstr(j,pat); if(!p) return 0;
  p=strchr(p+strlen(pat),':'); if(!p) return 0;
  p++; while(*p==' '||*p=='\t') p++;
  const char *s=p; int depth=0, str=0;
  for(;*p;p++){
    if(str){ if(*p=='\\' && p[1]) p++; else if(*p=='"'){ str=0; if(!depth){ p++; break; } } continue; }
    if(*p=='"'){ str=1; continue; }
    if(*p=='{'||*p=='[') depth++;
    else if(*p=='}'||*p==']'){ if(!depth) break; depth--; if(!depth){ p++; break; } }
    else if(*p==',' && !depth) break; }
  int n=(int)(p-s); if(n>=cap) return 0;
  memcpy(out,s,(size_t)n); out[n]=0; return n; }
static unsigned fnv(const char *s){ unsigned h=2166136261u; for(;*s;s++){ h^=(unsigned char)*s; h*=16777619u; } return h; }

static char *g_bbuf; static int g_bcap;
static int bidi(const char *fmt, ...){
  va_list ap; va_start(ap,fmt); int n=vsnprintf(g_bbuf,(size_t)g_bcap,fmt,ap); va_end(ap);
  if(n>=g_bcap){ g_bcap=n+1024; g_bbuf=realloc(g_bbuf,(size_t)g_bcap); va_start(ap,fmt); vsnprintf(g_bbuf,(size_t)g_bcap,fmt,ap); va_end(ap); }
  if(getenv("ASB_DEBUG")) fprintf(stderr,"bidi> %.200s\n",g_bbuf);
  return ws_send_text(g_ws,g_bbuf); }

/* synchronous call: send, then read until the reply with this id; events read meanwhile are handled */
static void on_bidi(const char *msg);
static buf_t g_sync; static int g_insync;
static const char *bidi_call(int id){
  buf_t b={0}; long long t0=now_ms(); g_insync++;
  while(now_ms()-t0<10000){
    fd_set rf; FD_ZERO(&rf); FD_SET(g_ws,&rf); struct timeval tv={0,200000};
    if(select(g_ws+1,&rf,NULL,NULL,&tv)<=0) continue;
    int k=ws_recv(g_ws,&b); if(k<0) break; if(k==0||!b.n) continue;
    if(strstr(b.p,"\"type\":\"event\"")==NULL && json_int(b.p,"id",0)==id){ free(g_sync.p); g_sync=b; g_insync--; return g_sync.p; }
    on_bidi(b.p); }
  free(b.p); g_insync--; return NULL; }

/* ---- input ---- */
static int g_mbtn=-1;
static int utf8_len(unsigned char c){ return c<0x80?1:c<0xE0?2:c<0xF0?3:4; }
static void bidi_keys(const char *text){ /* type UTF-8 text, one key per code point */
  char acts[16000]; int o=0;
  for(const unsigned char *p=(const unsigned char*)text;*p && o<(int)sizeof acts-200;){
    int l=utf8_len(*p); char v[16]; int vn=0;
    if(*p=='"'||*p=='\\'){ v[vn++]='\\'; v[vn++]=(char)*p; }
    else if(*p=='\n'||*p=='\r'){ memcpy(v,"\\uE007",6); vn=6; }
    else if(*p=='\t'){ memcpy(v,"\\uE004",6); vn=6; }
    else if(*p<32){ p++; continue; }
    else { for(int i=0;i<l && p[i];i++) v[vn++]=(char)p[i]; }
    v[vn]=0; p+=l;
    o+=snprintf(acts+o,sizeof acts-(size_t)o,"%s{\"type\":\"keyDown\",\"value\":\"%s\"},{\"type\":\"keyUp\",\"value\":\"%s\"}",o?",":"",v,v); }
  if(!o) return;
  int i=cdp_id();
  bidi("{\"id\":%d,\"method\":\"input.performActions\",\"params\":{\"context\":\"%s\",\"actions\":[{\"type\":\"key\",\"id\":\"kbd\",\"actions\":[%s]}]}}",i,g_ctx,acts); }
static const char *wd_key(const char *k){
  static const struct { const char *n, *v; } t[]={{"Enter","\\uE007"},{"Backspace","\\uE003"},{"Tab","\\uE004"},{"Escape","\\uE00C"},
    {"ArrowLeft","\\uE012"},{"ArrowUp","\\uE013"},{"ArrowRight","\\uE014"},{"ArrowDown","\\uE015"},{"Delete","\\uE017"},
    {"Home","\\uE011"},{"End","\\uE010"},{"PageUp","\\uE00E"},{"PageDown","\\uE00F"},{"Insert","\\uE016"},{" "," "},{NULL,NULL}};
  for(int i=0;t[i].n;i++) if(!strcmp(k,t[i].n)) return t[i].v;
  return NULL; }

/* ---- CDP -> BiDi ---- */
static void bidi_from_cdp(const char *cmd){
  int id=json_int(cmd,"id",0); char m[80]=""; json_str(cmd,"method",m,sizeof m);
  const char *P=strstr(cmd,"\"params\""); if(!P) P="";
  static char raw[16000];
  if(!strcmp(m,"Runtime.evaluate")){
    if(!json_raw(P,"expression",raw,sizeof raw)) return;
    bidi("{\"id\":%d,\"method\":\"script.evaluate\",\"params\":{\"expression\":%s,\"target\":{\"context\":\"%s\"},\"awaitPromise\":false,\"resultOwnership\":\"none\",\"userActivation\":true}}",id,raw,g_ctx); }
  else if(!strcmp(m,"Page.navigate")){
    if(!json_raw(P,"url",raw,sizeof raw)) return;
    bidi("{\"id\":%d,\"method\":\"browsingContext.navigate\",\"params\":{\"context\":\"%s\",\"url\":%s,\"wait\":\"none\"}}",id,g_ctx,raw); }
  else if(!strcmp(m,"Page.reload")) bidi("{\"id\":%d,\"method\":\"browsingContext.reload\",\"params\":{\"context\":\"%s\",\"wait\":\"none\"}}",id,g_ctx);
  else if(!strcmp(m,"Page.stopLoading")) bidi("{\"id\":%d,\"method\":\"script.evaluate\",\"params\":{\"expression\":\"window.stop()\",\"target\":{\"context\":\"%s\"},\"awaitPromise\":false}}",id,g_ctx);
  else if(!strcmp(m,"Page.captureScreenshot")) bidi("{\"id\":%d,\"method\":\"browsingContext.captureScreenshot\",\"params\":{\"context\":\"%s\"}}",id,g_ctx);
  else if(!strcmp(m,"Page.printToPDF")) bidi("{\"id\":%d,\"method\":\"browsingContext.print\",\"params\":{\"context\":\"%s\",\"background\":true}}",id,g_ctx);
  else if(!strcmp(m,"Page.bringToFront")) bidi("{\"id\":%d,\"method\":\"browsingContext.activate\",\"params\":{\"context\":\"%s\"}}",id,g_ctx);
  else if(!strcmp(m,"Emulation.setDeviceMetricsOverride")){
    char dsf[32]="1"; json_raw(P,"deviceScaleFactor",dsf,sizeof dsf);
    bidi("{\"id\":%d,\"method\":\"browsingContext.setViewport\",\"params\":{\"context\":\"%s\",\"viewport\":{\"width\":%d,\"height\":%d},\"devicePixelRatio\":%s}}",
         id,g_ctx,json_int(P,"width",1280),json_int(P,"height",720),dsf); }
  else if(!strcmp(m,"Page.addScriptToEvaluateOnNewDocument")){
    if(!json_raw(P,"source",raw,sizeof raw) || raw[0]!='"') return;
    if(strstr(raw,"navigator,'webdriver'")) return; /* Firefox: dom.webdriver.enabled=false in user.js does this; the override broke YouTube playback */
    unsigned h=fnv(raw); for(int i=0;i<g_npre;i++) if(g_pre_hash[i]==h) return;
    if(g_npre<16) g_pre_hash[g_npre++]=h;
    raw[strlen(raw)-1]=0; /* drop the closing quote: the source goes inside a function body */
    bidi("{\"id\":%d,\"method\":\"script.addPreloadScript\",\"params\":{\"functionDeclaration\":\"()=>{%s}\"}}",id,raw+1); }
  else if(!strcmp(m,"Runtime.addBinding")){
    char nm[40]=""; json_str(P,"name",nm,sizeof nm); if(!nm[0]) return;
    char key[60]; snprintf(key,sizeof key,"binding:%s",nm); unsigned h=fnv(key);
    for(int i=0;i<g_npre;i++) if(g_pre_hash[i]==h) return;
    if(g_npre<16) g_pre_hash[g_npre++]=h;
    bidi("{\"id\":%d,\"method\":\"script.addPreloadScript\",\"params\":{\"functionDeclaration\":\"(c)=>{window.%s=function(s){c(String(s))}}\",\"arguments\":[{\"type\":\"channel\",\"value\":{\"channel\":\"%s\"}}]}}",id,nm,nm); }
  else if(!strcmp(m,"Browser.setDownloadBehavior")){
    if(g_bidi_dl || !json_raw(P,"downloadPath",raw,sizeof raw)) return; g_bidi_dl=1;
    bidi("{\"id\":%d,\"method\":\"browser.setDownloadBehavior\",\"params\":{\"downloadBehavior\":{\"type\":\"allowed\",\"destinationFolder\":%s}}}",id,raw); }
  else if(!strcmp(m,"Input.insertText")){
    char t[8000]; if(json_str(P,"text",t,sizeof t)) bidi_keys(t); }
  else if(!strcmp(m,"Input.dispatchMouseEvent")){
    char ty[24]="", bt[16]=""; json_str(P,"type",ty,sizeof ty); json_str(P,"button",bt,sizeof bt);
    int x=json_int(P,"x",0), y=json_int(P,"y",0);
    if(x<0) x=0; if(y<0) y=0; if(x>=g_vw) x=g_vw-1; if(y>=g_vh) y=g_vh-1;
    if(!strcmp(ty,"mouseWheel")){
      bidi("{\"id\":%d,\"method\":\"input.performActions\",\"params\":{\"context\":\"%s\",\"actions\":[{\"type\":\"wheel\",\"id\":\"wheel\",\"actions\":[{\"type\":\"scroll\",\"x\":%d,\"y\":%d,\"deltaX\":0,\"deltaY\":%d,\"duration\":0}]}]}}",
           id,g_ctx,x,y,json_int(P,"deltaY",0)); return; }
    int b=!strcmp(bt,"middle")?1:!strcmp(bt,"right")?2:0;
    if(b==2) return; /* the client asks for the context menu with "ctx"; a native Firefox menu would sit on the hidden display */
    char act[200]="";
    if(!strcmp(ty,"mousePressed")){ snprintf(act,sizeof act,",{\"type\":\"pointerDown\",\"button\":%d}",b); g_mbtn=b; }
    else if(!strcmp(ty,"mouseReleased")){ if(g_mbtn<0) return; snprintf(act,sizeof act,",{\"type\":\"pointerUp\",\"button\":%d}",g_mbtn); g_mbtn=-1; }
    bidi("{\"id\":%d,\"method\":\"input.performActions\",\"params\":{\"context\":\"%s\",\"actions\":[{\"type\":\"pointer\",\"id\":\"mouse\",\"parameters\":{\"pointerType\":\"mouse\"},\"actions\":[{\"type\":\"pointerMove\",\"x\":%d,\"y\":%d,\"duration\":0}%s]}]}}",
         id,g_ctx,x,y,act); }
  else if(!strcmp(m,"Input.dispatchKeyEvent")){
    char ty[16]="", k[32]=""; json_str(P,"type",ty,sizeof ty); json_str(P,"key",k,sizeof k);
    if(strcmp(ty,"keyDown")) return;  /* each keyDown is sent as a full press */
    const char *v=wd_key(k); char one[8];
    if(!v){ if(strlen(k)!=1) return; if(k[0]=='"'||k[0]=='\\') snprintf(one,sizeof one,"\\%c",k[0]); else snprintf(one,sizeof one,"%c",k[0]); v=one; }
    int mods=json_int(P,"modifiers",0);
    static const struct { int bit; const char *v; } mk[]={{2,"\\uE009"},{1,"\\uE00A"},{4,"\\uE03D"},{8,"\\uE008"}};
    char a[600]; int o=0;
    for(int i=0;i<4;i++) if((mods&mk[i].bit) && !(mk[i].bit==8 && strlen(k)==1)) o+=snprintf(a+o,sizeof a-(size_t)o,"{\"type\":\"keyDown\",\"value\":\"%s\"},",mk[i].v);
    o+=snprintf(a+o,sizeof a-(size_t)o,"{\"type\":\"keyDown\",\"value\":\"%s\"},{\"type\":\"keyUp\",\"value\":\"%s\"}",v,v);
    for(int i=3;i>=0;i--) if((mods&mk[i].bit) && !(mk[i].bit==8 && strlen(k)==1)) o+=snprintf(a+o,sizeof a-(size_t)o,",{\"type\":\"keyUp\",\"value\":\"%s\"}",mk[i].v);
    bidi("{\"id\":%d,\"method\":\"input.performActions\",\"params\":{\"context\":\"%s\",\"actions\":[{\"type\":\"key\",\"id\":\"kbd\",\"actions\":[%s]}]}}",id,g_ctx,a); }
  else if(!strcmp(m,"Page.enable")||!strcmp(m,"Runtime.enable")||!strcmp(m,"Network.enable")||!strcmp(m,"Page.screencastFrameAck")
       ||!strcmp(m,"Page.startScreencast")||!strcmp(m,"Page.stopScreencast")) ;
  else fprintf(stderr,"asb-chromed: [firefox] no BiDi equivalent for %s\n",m);
}

/* ---- frames: poll captureScreenshot ---- */
static void bidi_cast_tick(void){
  if(!g_bidi || !g_screencast_on || g_cli<0 || g_ws<0 || g_cast_id || !g_ctx[0]) return;
  if(now_ms()<g_cast_due) return;
  if((g_ff&ASBC_FF_ACK) && g_inflight>=2) return; /* client still drawing */
  g_cast_id=cdp_id();
  bidi("{\"id\":%d,\"method\":\"browsingContext.captureScreenshot\",\"params\":{\"context\":\"%s\",\"origin\":\"viewport\"}}",g_cast_id,g_ctx); }

/* ---- tabs ---- */
static int bidi_tree(void){ /* fills g_tabs (id = context) from browsingContext.getTree */
  struct { char id[80], title[128]; } keep[MAXTAB]; int nk=0;
  for(int i=0;i<g_ntab && nk<MAXTAB;i++){ scopy(keep[nk].id,sizeof keep[0].id,g_tabs[i].id); scopy(keep[nk].title,sizeof keep[0].title,g_tabs[i].title); nk++; }
  int id=cdp_id(); bidi("{\"id\":%d,\"method\":\"browsingContext.getTree\",\"params\":{\"maxDepth\":0}}",id);
  const char *r=bidi_call(id); if(!r) return -1;
  g_ntab=0;
  const char *p=r;
  while(g_ntab<MAXTAB && (p=strstr(p,"{\"children\""))!=NULL){
    const char *e=strstr(p+1,"{\"children\""); size_t len=e?(size_t)(e-p):strlen(p);
    char one[3000]; if(len>=sizeof one) len=sizeof one-1; memcpy(one,p,len); one[len]=0;
    char c[80]="", u[2400]=""; json_str(one,"context",c,sizeof c); json_str(one,"url",u,sizeof u);
    if(c[0]){ memset(&g_tabs[g_ntab],0,sizeof g_tabs[0]);
      scopy(g_tabs[g_ntab].id,sizeof g_tabs[0].id,c); scopy(g_tabs[g_ntab].url,sizeof g_tabs[0].url,u);
      for(int k=0;k<nk;k++) if(!strcmp(keep[k].id,c)) scopy(g_tabs[g_ntab].title,sizeof g_tabs[0].title,keep[k].title);
      g_ntab++; }
    p+=len; }
  return 0; }
static int bidi_tab_create(const char *url){ /* returns index of the new tab or -1 */
  int id=cdp_id(); bidi("{\"id\":%d,\"method\":\"browsingContext.create\",\"params\":{\"type\":\"tab\"}}",id);
  const char *r=bidi_call(id); char c[80]=""; if(r) json_str(r,"context",c,sizeof c);
  if(!c[0]) return -1;
  if(url && url[0]){ char e[2600]; jesc(url,e,sizeof e); int i=cdp_id();
    bidi("{\"id\":%d,\"method\":\"browsingContext.navigate\",\"params\":{\"context\":\"%s\",\"url\":\"%s\",\"wait\":\"none\"}}",i,c,e); }
  load_tabs(); return tab_by_id(c); }
static void bidi_tab_close(const char *ctx){
  int id=cdp_id(); bidi("{\"id\":%d,\"method\":\"browsingContext.close\",\"params\":{\"context\":\"%s\",\"promptUnload\":false}}",id,ctx); bidi_call(id); }

/* ---- BiDi -> CDP shapes ---- */
static void on_bidi(const char *msg){
  if(getenv("ASB_DEBUG")) fprintf(stderr,"bidi< %.200s\n",msg);
  if(!strstr(msg,"\"type\":\"event\"")){ /* reply */
    int rid=json_int(msg,"id",0);
    if(rid && rid==g_cast_id){
      g_cast_id=0;
      int n=json_b64_field(msg,"data",g_cap,(int)sizeof g_cap);
      if(n>0 && g_cli>=0){ g_frames_seen++;
        long long t0=us_now(); int qoi=g_pf==ASBC_PF_QOI; g_sent_by_q=0; frame_png(n); long long dt=us_now()-t0;
        g_enc_us+=dt; g_enc_n++; if(qoi) g_sent_by+=g_sent_by_q; g_sent_fr++; }
      g_cast_due=now_ms()+g_cast_ms;
      return; }
    on_cdp(msg); return; }
  char m[80]=""; json_str(msg,"method",m,sizeof m);
  char c[80]=""; json_str(msg,"context",c,sizeof c);
  char s[3200];
  if(!strcmp(m,"browsingContext.contextCreated")||!strcmp(m,"browsingContext.contextDestroyed")){
    if(strstr(msg,"\"parent\":null")) g_tabs_dirty=1; return; }
  if(strcmp(c,g_ctx)){ /* other tabs and iframes: only downloads matter */
    if(strncmp(m,"browsingContext.download",24)) return; }
  if(!strcmp(m,"browsingContext.navigationStarted")||!strcmp(m,"browsingContext.fragmentNavigated")||!strcmp(m,"browsingContext.historyUpdated")){
    char u[2400]=""; json_raw(msg,"url",u,sizeof u); if(!u[0]) return;
    snprintf(s,sizeof s,"{\"method\":\"%s\",\"params\":{\"frame\":{\"url\":%s}}}",!strcmp(m,"browsingContext.navigationStarted")?"Page.frameNavigated":"Page.navigatedWithinDocument",u);
    on_cdp(s); return; }
  if(!strcmp(m,"browsingContext.domContentLoaded")){ on_cdp("{\"method\":\"Page.domContentEventFired\"}"); return; }
  if(!strcmp(m,"browsingContext.load")){ on_cdp("{\"method\":\"Page.loadEventFired\"}"); return; }
  if(!strcmp(m,"browsingContext.downloadWillBegin")){
    char nav[64]="", fn[600]="\"file\"", u[2400]="\"\""; json_str(msg,"navigation",nav,sizeof nav); json_raw(msg,"suggestedFilename",fn,sizeof fn); json_raw(msg,"url",u,sizeof u);
    snprintf(s,sizeof s,"{\"method\":\"Browser.downloadWillBegin\",\"params\":{\"guid\":\"%s\",\"suggestedFilename\":%s,\"url\":%s}}",nav,fn,u);
    on_cdp(s); return; }
  if(!strcmp(m,"browsingContext.downloadEnd")){
    char nav[64]="", st[24]=""; json_str(msg,"navigation",nav,sizeof nav); json_str(msg,"status",st,sizeof st);
    double got=0; char fp[1200]=""; if(json_str(msg,"filepath",fp,sizeof fp)){ struct stat sb; if(!stat(fp,&sb)) got=(double)sb.st_size; }
    snprintf(s,sizeof s,"{\"method\":\"Browser.downloadProgress\",\"params\":{\"guid\":\"%s\",\"state\":\"%s\",\"receivedBytes\":%.0f,\"totalBytes\":%.0f}}",nav,!strcmp(st,"complete")?"completed":"canceled",got,got);
    on_cdp(s); return; }
  if(!strcmp(m,"script.message")){
    char ch[40]="", pl[3000]="";
    json_str(msg,"channel",ch,sizeof ch);
    const char *d=strstr(msg,"\"data\""); if(!d || !json_raw(d,"value",pl,sizeof pl)) return;
    snprintf(s,sizeof s,"{\"method\":\"Runtime.bindingCalled\",\"params\":{\"name\":\"%s\",\"payload\":%s}}",ch,pl);
    on_cdp(s); return; }
}

/* ---- start / stop ---- */
static int bidi_session(int port){
  char url[80]; snprintf(url,sizeof url,"ws://127.0.0.1:%d/session",port);
  g_ws=ws_open(url); if(g_ws<0) return -1;
  if(!g_bbuf){ g_bcap=65536; g_bbuf=malloc((size_t)g_bcap); }
  int id=cdp_id();
  bidi("{\"id\":%d,\"method\":\"session.new\",\"params\":{\"capabilities\":{\"alwaysMatch\":{\"unhandledPromptBehavior\":{\"default\":\"ignore\"}}}}}",id);
  const char *r=bidi_call(id); if(!r || strstr(r,"\"type\":\"error\"")){ fprintf(stderr,"asb-chromed: [firefox] session.new failed: %.200s\n",r?r:"timeout"); return -1; }
  { char ver[40]=""; json_str(r,"browserVersion",ver,sizeof ver); fprintf(stderr,"asb-chromed: Firefox %s over WebDriver BiDi\n",ver); }
  id=cdp_id();
  bidi("{\"id\":%d,\"method\":\"session.subscribe\",\"params\":{\"events\":[\"browsingContext.contextCreated\",\"browsingContext.contextDestroyed\","
       "\"browsingContext.navigationStarted\",\"browsingContext.fragmentNavigated\",\"browsingContext.historyUpdated\",\"browsingContext.domContentLoaded\","
       "\"browsingContext.load\",\"browsingContext.downloadWillBegin\",\"browsingContext.downloadEnd\",\"script.message\"]}}",id);
  r=bidi_call(id);
  if(r && strstr(r,"\"type\":\"error\"")){ /* older Firefox without some events: subscribe to the core set */
    id=cdp_id();
    bidi("{\"id\":%d,\"method\":\"session.subscribe\",\"params\":{\"events\":[\"browsingContext.contextCreated\",\"browsingContext.contextDestroyed\","
         "\"browsingContext.navigationStarted\",\"browsingContext.fragmentNavigated\",\"browsingContext.domContentLoaded\",\"browsingContext.load\",\"script.message\"]}}",id);
    bidi_call(id); }
  if(bidi_tree() || g_ntab<1) return -1;
  g_tab=0; scopy(g_ctx,sizeof g_ctx,g_tabs[0].id);
  return 0; }

static const char *firefox_bin(void){
  const char *b=getenv("ASB_FIREFOX"); if(b&&b[0]) return b;
  static const char *c[]={"/opt/firefox/firefox","/usr/lib/firefox/firefox","/usr/bin/firefox-esr","/usr/bin/firefox",NULL};
  for(int i=0;c[i];i++) if(!access(c[i],X_OK)) return c[i];
  return NULL; }
static int spawn_firefox(const char *profile, int port){
  const char *bin=firefox_bin(); if(!bin){ fprintf(stderr,"asb-chromed: no Firefox binary (set ASB_FIREFOX)\n"); return -1; }
  mkdir(profile,0700);
  char uj[700]; snprintf(uj,sizeof uj,"%s/user.js",profile);
  FILE *f=fopen(uj,"w");
  if(f){ fprintf(f,
    "user_pref(\"browser.shell.checkDefaultBrowser\", false);\n"
    "user_pref(\"browser.aboutwelcome.enabled\", false);\n"
    "user_pref(\"browser.startup.homepage_override.mstone\", \"ignore\");\n"
    "user_pref(\"browser.startup.page\", 0);\n"
    "user_pref(\"startup.homepage_welcome_url\", \"\");\n"
    "user_pref(\"datareporting.policy.dataSubmissionEnabled\", false);\n"
    "user_pref(\"toolkit.telemetry.reportingpolicy.firstRun\", false);\n"
    "user_pref(\"browser.tabs.warnOnClose\", false);\n"
    "user_pref(\"browser.sessionstore.resume_from_crash\", false);\n"
    "user_pref(\"browser.download.folderList\", 2);\n"
    "user_pref(\"browser.download.dir\", \"%s\");\n"
    "user_pref(\"browser.download.useDownloadDir\", true);\n"
    "user_pref(\"browser.download.always_ask_before_handling_new_types\", false);\n"
    "user_pref(\"browser.download.alwaysOpenPanel\", false);\n"
    "user_pref(\"media.autoplay.default\", 0);\n"
    "user_pref(\"dom.webdriver.enabled\", false);\n"
    "user_pref(\"app.update.auto\", false);\n", g_dlpath[0]?g_dlpath:"/tmp");
    fclose(f); }
  ensure_xvfb();
  fprintf(stderr,"asb-chromed: Firefox %s DISPLAY=%s profile %s\n",bin,getenv("DISPLAY"),profile);
  char ps[16], ws_[16], hs[16]; snprintf(ps,sizeof ps,"%d",port); snprintf(ws_,sizeof ws_,"%d",g_vw); snprintf(hs,sizeof hs,"%d",g_vh+80);
  pid_t pid=fork(); if(pid<0) return -1;
  if(pid==0){
    int n=open("/dev/null",O_RDWR); if(n>=0){ dup2(n,0); dup2(n,1); if(!getenv("ASB_DEBUG")) dup2(n,2); }
    setenv("MOZ_CRASHREPORTER_DISABLE","1",1);
    execl(bin,bin,"--remote-debugging-port",ps,"--profile",profile,"--no-remote","--new-instance","-width",ws_,"-height",hs,"about:blank",(char*)0);
    _exit(127); }
  g_chrome=pid; return 0; }
