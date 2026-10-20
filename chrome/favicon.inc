/* favicon.inc — 0.88: 16x16 site icons for the client's bookmarks bar (a beta tester). Included by host.c after features.inc.
   The client asks "favicon HOST" once per site (it keeps the answer in PROGDIR:Data/Favicons/ and never asks again),
   and gets ASBC_FAVICON: u8 nhost, host, u8 w, u8 h, w*h*4 RGBA (w=h=0: the site has none, the client shows its
   default icon). So a site costs the Amiga about 1 KB once. The PC side fetches with curl in a child process
   (https://HOST/favicon.ico, else the icon the home page links), decodes ICO/PNG/GIF/BMP/JPEG, scales to 16x16 and
   keeps the result in <config>/favicons/ (HOST.rgba, or HOST.none for a site that answered without a usable icon,
   looked at again after 30 days). A site that cannot be reached gets no answer at all, so nothing is remembered. */
#define FAV_PEND 8
static struct { char host[128]; pid_t pid; long long t0; } g_fav[FAV_PEND];
static int fav_host_ok(const char *h){
  if(!h[0]||strlen(h)>100) return 0;
  for(const char *p=h;*p;p++) if(!(isalnum((unsigned char)*p)||*p=='.'||*p=='-'||*p==':')) return 0;
  return h[0]!='-'; }
static void fav_path(char *o,int cap,const char *host,const char *ext){
  char d[700]; cfgpath(d,sizeof d,"favicons"); mkdir(d,0755);
  char h[128]; scopy(h,sizeof h,host); for(char *p=h;*p;p++) if(*p==':') *p='_';
  snprintf(o,(size_t)cap,"%s/%s%s",d,h,ext); }
static void fav_send(const char *host,const unsigned char *rgba){
  if(g_cli<0) return;
  unsigned char b[3+128+16*16*4]; int hl=(int)strlen(host); if(hl>127) hl=127;
  int o=0; b[o++]=(unsigned char)hl; memcpy(b+o,host,(size_t)hl); o+=hl;
  if(rgba){ b[o++]=16; b[o++]=16; memcpy(b+o,rgba,16*16*4); o+=16*16*4; } else { b[o++]=0; b[o++]=0; }
  asbc_send(g_cli,ASBC_FAVICON,b,(uint32_t)o); }
/* answer from the cache; 1 = answered */
static int fav_cached(const char *host){
  char p[800]; fav_path(p,sizeof p,host,".rgba");
  FILE *f=fopen(p,"rb"); if(f){ unsigned char px[16*16*4]; size_t n=fread(px,1,sizeof px,f); fclose(f); if(n==sizeof px){ fav_send(host,px); return 1; } }
  fav_path(p,sizeof p,host,".none"); struct stat st;
  if(stat(p,&st)==0){ if(time(NULL)-st.st_mtime>30L*86400){ unlink(p); return 0; } fav_send(host,NULL); return 1; }   /* look again after 30 days */
  return 0; }

/* ---- decoding (runs in the child) ---- */
static unsigned char *fav_dib(const unsigned char *b,int n,int *w,int *h){   /* an ICO entry without PNG: BITMAPINFOHEADER + pixels + AND mask */
  if(n<40) return NULL;
  int hs=(int)(b[0]|b[1]<<8|b[2]<<16|(unsigned)b[3]<<24), W=(int)(b[4]|b[5]<<8|b[6]<<16|(unsigned)b[7]<<24), H=(int)(b[8]|b[9]<<8|b[10]<<16|(unsigned)b[11]<<24)/2;
  int bpp=b[14]|b[15]<<8, ncol=(int)(b[32]|b[33]<<8);
  if(hs<40||W<1||H<1||W>256||H>256||(bpp!=32&&bpp!=24&&bpp!=8&&bpp!=4&&bpp!=1)) return NULL;
  if(bpp<=8 && !ncol) ncol=1<<bpp;
  const unsigned char *pal=b+hs, *px=pal+(bpp<=8?ncol*4:0);
  int stride=((W*bpp+31)/32)*4, mstride=((W+31)/32)*4;
  const unsigned char *mask=px+stride*H;
  if(mask-b>n) return NULL;
  int hasmask=mask+mstride*H-b<=n;
  unsigned char *o=malloc((size_t)W*H*4); if(!o) return NULL;
  int anyalpha=0;
  for(int y=0;y<H;y++){ const unsigned char *r=px+(H-1-y)*stride; unsigned char *d=o+(size_t)y*W*4;
    for(int x=0;x<W;x++,d+=4){
      if(bpp==32){ d[0]=r[x*4+2]; d[1]=r[x*4+1]; d[2]=r[x*4]; d[3]=r[x*4+3]; if(d[3]) anyalpha=1; continue; }
      if(bpp==24){ d[0]=r[x*3+2]; d[1]=r[x*3+1]; d[2]=r[x*3]; d[3]=255; continue; }
      int i=bpp==8?r[x]:bpp==4?(r[x/2]>>((x&1)?0:4))&15:(r[x/8]>>(7-(x&7)))&1;
      if(i>=ncol) i=0;
      d[0]=pal[i*4+2]; d[1]=pal[i*4+1]; d[2]=pal[i*4]; d[3]=255; } }
  if(hasmask && !(bpp==32&&anyalpha))
    for(int y=0;y<H;y++){ const unsigned char *m=mask+(H-1-y)*mstride; for(int x=0;x<W;x++) if((m[x/8]>>(7-(x&7)))&1) o[((size_t)y*W+x)*4+3]=0; }
  *w=W; *h=H; return o; }
static unsigned char *fav_decode(const unsigned char *b,int n,int *w,int *h){
  if(n>=6 && b[0]==0 && b[1]==0 && b[2]==1 && b[3]==0){   /* ICO: the entry closest to 16 px (bigger first), most colours */
    int cnt=b[4]|b[5]<<8, best=-1, bs=1<<30;
    for(int i=0;i<cnt && 6+16*i+16<=n;i++){ const unsigned char *e=b+6+16*i; int sz=e[0]?e[0]:256, bpp=e[6]|e[7]<<8;
      int score=(sz>=16?(sz-16)*4:(16-sz)*16+600)-bpp; if(score<bs){ bs=score; best=i; } }
    if(best<0) return NULL;
    const unsigned char *e=b+6+16*best; long len=(long)(e[8]|e[9]<<8|e[10]<<16|(unsigned long)e[11]<<24), off=(long)(e[12]|e[13]<<8|e[14]<<16|(unsigned long)e[15]<<24);
    if(off<0||len<=0||off+len>n) return NULL;
    if(len>8 && !memcmp(b+off,"\x89PNG",4)){ int c; return stbi_load_from_memory(b+off,(int)len,w,h,&c,4); }
    return fav_dib(b+off,(int)len,w,h); }
  int c; return stbi_load_from_memory(b,n,w,h,&c,4); }
static void fav_scale(const unsigned char *s,int W,int H,unsigned char *o){   /* box filter to 16x16, alpha weighted */
  for(int y=0;y<16;y++) for(int x=0;x<16;x++){
    int x0=x*W/16, x1=(x+1)*W/16, y0=y*H/16, y1=(y+1)*H/16; if(x1<=x0) x1=x0+1; if(y1<=y0) y1=y0+1;
    long r=0,g=0,bl=0,a=0,cnt=0;
    for(int j=y0;j<y1&&j<H;j++) for(int i=x0;i<x1&&i<W;i++){ const unsigned char *p=s+((size_t)j*W+i)*4; r+=p[0]*p[3]; g+=p[1]*p[3]; bl+=p[2]*p[3]; a+=p[3]; cnt++; }
    unsigned char *d=o+(y*16+x)*4;
    if(a){ d[0]=(unsigned char)(r/a); d[1]=(unsigned char)(g/a); d[2]=(unsigned char)(bl/a); } else d[0]=d[1]=d[2]=0;
    d[3]=(unsigned char)(cnt?a/cnt:0); } }
static unsigned char *fav_read(const char *p,int *n){
  FILE *f=fopen(p,"rb"); if(!f) return NULL; unsigned char *b=malloc(600*1024); if(!b){ fclose(f); return NULL; }
  *n=(int)fread(b,1,600*1024,f); fclose(f); return b; }
static int fav_curl(const char *url,const char *out,const char *maxsz){
  pid_t p=fork(); if(p<0) return -1;
  if(p==0){ int dn=open("/dev/null",O_RDWR); if(dn>=0){ dup2(dn,0); dup2(dn,1); dup2(dn,2); }
    execlp("curl","curl","-sfL","--max-time","10","--max-filesize",maxsz,"-A","Mozilla/5.0 (X11; Linux x86_64) AmiSurf","-o",out,url,(char*)0); _exit(127); }
  int st=0; if(waitpid(p,&st,0)<0) return -1; return WIFEXITED(st)?WEXITSTATUS(st):-1; }
static int fav_try(const char *url,const char *tmp,unsigned char *o16){
  unlink(tmp); if(fav_curl(url,tmp,"300000")) return 0;
  int n=0; unsigned char *b=fav_read(tmp,&n); unlink(tmp); if(!b) return 0;
  int w=0,h=0; unsigned char *px=fav_decode(b,n,&w,&h); free(b); if(!px) return 0;
  fav_scale(px,w,h,o16); free(px);
  int vis=0; for(int i=0;i<256;i++) if(o16[i*4+3]>32) vis++;
  return vis>8; }
/* the <link rel=icon href=...> of the home page; prefers a PNG/ICO one */
static int fav_link(const char *html,const char *host,char *out,int cap){
  const char *p=html; int found=0;
  while((p=strcasestr(p,"<link"))){ const char *e=strchr(p,'>'); if(!e) break;
    char tag[1200]; int l=(int)(e-p); if(l>(int)sizeof tag-1) l=(int)sizeof tag-1; memcpy(tag,p,(size_t)l); tag[l]=0; p=e;
    const char *rel=strcasestr(tag,"rel="); if(!rel||!strcasestr(rel,"icon")||strcasestr(tag,"mask-icon")) continue;
    const char *hr=strcasestr(tag,"href="); if(!hr) continue; hr+=5; char q=*hr=='"'||*hr=='\''?*hr++:' ';
    char href[900]; int k=0; while(hr[k] && hr[k]!=q && hr[k]!='>' && !(q==' '&&isspace((unsigned char)hr[k])) && k<(int)sizeof href-1){ href[k]=hr[k]; k++; } href[k]=0;
    if(!href[0]||strstr(href,".svg")||!strncmp(href,"data:",5)) continue;
    if(!strncmp(href,"http",4)) snprintf(out,(size_t)cap,"%s",href);
    else if(!strncmp(href,"//",2)) snprintf(out,(size_t)cap,"https:%s",href);
    else if(href[0]=='/') snprintf(out,(size_t)cap,"https://%s%s",host,href);
    else snprintf(out,(size_t)cap,"https://%s/%s",host,href);
    found=1; if(strcasestr(href,".png")||strcasestr(href,".ico")) return 1; }
  return found; }
static void fav_child(const char *host){
  signal(SIGCHLD,SIG_DFL);
  char tmp[800], res[800], url[1000]; unsigned char o16[16*16*4];
  fav_path(tmp,sizeof tmp,host,".tmp");
  snprintf(url,sizeof url,"https://%s/favicon.ico",host);
  int ok=fav_try(url,tmp,o16);
  int reached=ok;
  if(!ok){ unlink(tmp); snprintf(url,sizeof url,"https://%s/",host);
    int rc=fav_curl(url,tmp,"600000"); reached=rc==0||rc==22||rc==63;   /* the site answered (22: HTTP error, 63: too big) */
    if(!rc){ int n=0; unsigned char *b=fav_read(tmp,&n); unlink(tmp);
      if(b){ b[n<600*1024?n:600*1024-1]=0; char ic[1000]; if(fav_link((char*)b,host,ic,sizeof ic)) ok=fav_try(ic,tmp,o16); free(b); } } }
  if(ok){ fav_path(res,sizeof res,host,".part"); FILE *f=fopen(res,"wb"); if(f){ fwrite(o16,1,sizeof o16,f); fclose(f); char fin[800]; fav_path(fin,sizeof fin,host,".rgba"); rename(res,fin); } }
  else if(reached){ fav_path(res,sizeof res,host,".none"); FILE *f=fopen(res,"w"); if(f) fclose(f); }
  _exit(0); }   /* not reached (offline, DNS): no file, so nothing is remembered; fav_tick gives up quietly */
static void fav_request(const char *host){
  if(!fav_host_ok(host)){ fav_send(host,NULL); return; }
  if(fav_cached(host)) return;
  int free_i=-1; for(int i=0;i<FAV_PEND;i++){ if(g_fav[i].host[0] && !strcmp(g_fav[i].host,host)) return; if(!g_fav[i].host[0] && free_i<0) free_i=i; }
  if(free_i<0) return;   /* busy: no answer; the client shows its default icon and asks again next run */
  pid_t p=fork(); if(p<0) return;
  if(p==0){ if(g_listen>=0) close(g_listen); fav_child(host); }
  scopy(g_fav[free_i].host,sizeof g_fav[0].host,host); g_fav[free_i].pid=p; g_fav[free_i].t0=now_ms();
  fprintf(stderr,"asb-chromed: favicon %s: fetching\n",host); }
static void fav_tick(void){
  static long long last; long long t=now_ms(); if(t-last<200) return; last=t;
  for(int i=0;i<FAV_PEND;i++){ if(!g_fav[i].host[0]) continue;
    if(fav_cached(g_fav[i].host)){ fprintf(stderr,"asb-chromed: favicon %s: done\n",g_fav[i].host); g_fav[i].host[0]=0; continue; }
    if(t-g_fav[i].t0>30000){ fprintf(stderr,"asb-chromed: favicon %s: gave up\n",g_fav[i].host); kill(g_fav[i].pid,SIGKILL); g_fav[i].host[0]=0; } } }   /* no answer: nothing is remembered on either side */
