/* ---- 0.87: bookmarks bar and Bookmarks menu (all three programs) ----
   The bookmarks stay where they always were, in the engine's bookmarks.tsv on the PC (same format; 0.87 adds an
   optional 4th column, the folder, set on the Manage bookmarks page). After connecting AmiSurf asks for the list
   ("bookmarks"); an 0.87+ engine answers with ASBC_BMARKS and sends it again whenever it changes. An older engine
   does not answer: the bar then shows one "Bookmarks" button (the bookmarks page) and the menu says why.
   The bar: bookmarks without a folder first, then one button per folder, then a "\xBB" button with whatever did
   not fit. Click opens; Shift-click (and middle-click in the GadTools build) opens in a new tab.
   BOOKMARKSBAR=ON|OFF (Data/AmiSurf.prefs or tooltype), Amiga-B toggles it.
   0.88: AmiSurf asks "bookmarks 2"; a 0.88 engine answers with a "#2" line and a 4th field, flags ("h": not on the
   bar, the Bookmarks page's "Show on the bookmarks bar" box / bookmarks.tsv's 5th column). Such bookmarks stay in
   the Bookmarks menu. Right-click on the bar: Open / Open in new tab / Edit... / Hide from bookmarks bar /
   Hide bookmarks bar / Manage bookmarks (Edit and Hide need the 0.88 engine). */
#include <devices/input.h>
#include <proto/input.h>
struct Device *InputBase;
static struct IOStdReq g_inreq; static int g_inopen;
static int qual_shift(void){   /* Shift held right now (input.device PeekQualifier) */
  if(!g_inopen){ g_inopen=-1; memset(&g_inreq,0,sizeof g_inreq);
    if(!OpenDevice((STRPTR)"input.device",0,(struct IORequest*)&g_inreq,0)){ InputBase=g_inreq.io_Device; g_inopen=1; } }
  if(g_inopen<0||!InputBase) return 0;
  return !!(PeekQualifier()&(IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT)); }
static void qual_close(void){ if(g_inopen>0){ CloseDevice((struct IORequest*)&g_inreq); g_inopen=0; InputBase=NULL; } }

#define BMMAX 200
#define BMFMAX 16
static struct { char title[48]; char *url; int folder; int nobar; } g_bm[BMMAX];   /* folder: -1 none, else index in g_bmf */
static char g_bmf[BMFMAX][32]; static int g_nbm, g_nbmf;
static int g_bm_known;            /* the engine sent a list (0.87+ engine) */
static int g_bm_v2;               /* 0.88+ engine: flags field, bmhide/bmshow/bmedit, favicons */
static void bm_free(void){ for(int i=0;i<g_nbm;i++){ free(g_bm[i].url); g_bm[i].url=NULL; } g_nbm=g_nbmf=0; }
/* UTF-8 -> Latin-1 (Amiga fonts); other characters become '?' */
static void u8_l1(char *d,size_t cap,const char *s,int n){
  size_t o=0; const unsigned char *p=(const unsigned char*)s, *e=p+n;
  while(p<e && o+1<cap){ unsigned c=*p++;
    if(c>=0xC0 && c<0xE0 && p<e){ c=((c&0x1F)<<6)|(*p++&0x3F); }
    else if(c>=0xE0){ int k=c>=0xF0?3:2; while(k-- && p<e && (*p&0xC0)==0x80) p++; c='?'; }
    else if(c>=0x80) c='?';
    if(c<32) c=' '; d[o++]=(char)(c>255?'?':c); }
  d[o]=0; }
static void bm_apply(const unsigned char *p,int n){   /* ASBC_BMARKS */
  bm_free(); g_bm_known=1; g_bm_v2=0;
  const unsigned char *e=p+n;
  if(n>=3 && p[0]=='#' && p[1]=='2' && p[2]=='\n'){ g_bm_v2=1; p+=3; }
  while(p<e && g_nbm<BMMAX){
    const unsigned char *nl=p; while(nl<e && *nl!='\n') nl++;
    const unsigned char *t1=p; while(t1<nl && *t1!='\t') t1++;
    const unsigned char *t2=t1<nl?t1+1:nl; while(t2<nl && *t2!='\t') t2++;
    const unsigned char *t3=t2<nl?t2+1:nl; if(g_bm_v2) while(t3<nl && *t3!='\t') t3++; else t3=nl;   /* 0.88: 4th field flags */
    int ul=(int)(t1-p);
    if(ul>0){
      char *u=malloc((size_t)ul+1); if(!u) break; memcpy(u,p,(size_t)ul); u[ul]=0;
      char tt[48]; if(t1<nl) u8_l1(tt,sizeof tt,(const char*)t1+1,(int)(t2-t1-1)); else tt[0]=0;
      char fo[32]=""; if(t2<nl) u8_l1(fo,sizeof fo,(const char*)t2+1,(int)(t3-t2-1));
      int nobar=0; for(const unsigned char *q=t3;q<nl;q++) if(*q=='h') nobar=1;
      int f=-1; if(fo[0]){ for(int k=0;k<g_nbmf;k++) if(!strcmp(g_bmf[k],fo)) f=k;
        if(f<0 && g_nbmf<BMFMAX){ f=g_nbmf++; scopy_u(g_bmf[f],sizeof g_bmf[0],fo); } }
      g_bm[g_nbm].url=u; g_bm[g_nbm].folder=f; g_bm[g_nbm].nobar=nobar;
      if(tt[0]) scopy_u(g_bm[g_nbm].title,sizeof g_bm[0].title,tt);
      else { const char *h=strstr(u,"://"); h=h?h+3:u; scopy_u(g_bm[g_nbm].title,sizeof g_bm[0].title,h); }
      g_nbm++; }
    p=nl+1; }
  { int h=0; for(int i=0;i<g_nbm;i++) h+=g_bm[i].nobar; asb_log("bookmarks: %d (%d folders%s, %d not on the bar) from the engine",g_nbm,g_nbmf,g_bm_v2?"":", 0.87 engine",h); }
  g_bm_dirty=1; }
static void bm_open(int i,int newtab){
  if(i<0||i>=g_nbm||!g_bm[i].url) return;
  CRUMB("bookmark %d%s",i,newtab?" (new tab)":"");
  if(newtab) send_cmd("newtab %s",g_bm[i].url); else send_nav(g_bm[i].url); }

/* only the BOOKMARKSBAR line of the prefs file changes (Settings "Use" values stay unsaved) */
static void prefs_set_bmbar(void){
  char buf[4000]; int n=0; FILE *f=fopen(PREF_FILE,"r");
  if(f){ char l[2400]; while(fgets(l,sizeof l,f)){ if(!strncasecmp(l,"BOOKMARKSBAR=",13)) continue; int k=(int)strlen(l); if(n+k<(int)sizeof buf-40){ memcpy(buf+n,l,(size_t)k); n+=k; } } fclose(f); }
  n+=snprintf(buf+n,sizeof buf-(size_t)n,"BOOKMARKSBAR=%s\n",g_bmbar?"ON":"OFF");
  data_dir(); f=fopen(PREF_FILE,"w"); if(f){ fwrite(buf,1,(size_t)n,f); fclose(f); } }
static void bmbar_apply(void);   /* front end: show/hide the bar */
static void bmbar_toggle(void){
  g_bmbar=!g_bmbar; asb_log("bookmarks bar %s",g_bmbar?"on":"off"); prefs_set_bmbar(); bmbar_apply();
  set_status(g_bmbar?"Bookmarks bar on (Amiga B)":"Bookmarks bar off (Amiga B)"); }

/* bar layout: slot k shows a bookmark (>=0) or a folder (-1-f); slots that do not fit go behind "\xBB" */
#define BMSLOTS 40
static int g_slot[BMSLOTS], g_slotx[BMSLOTS], g_slotw[BMSLOTS], g_nslot, g_nshown, g_morex, g_morew;
static void slot_label(int s,char *b,int cap){
  if(s>=0){ const char *t=g_bm[s].title; int l=(int)strlen(t); if(l>22) snprintf(b,(size_t)cap,"%.20s...",t); else snprintf(b,(size_t)cap,"%s",t); }
  else snprintf(b,(size_t)cap,"%.20s \xBB",g_bmf[-1-s]); }
/* measure(label) = button width in pixels; avail = bar width; gap = space between buttons */
static void bm_layout(int avail,int (*measure)(const char*),int gap){
  g_nslot=0;
  for(int i=0;i<g_nbm && g_nslot<BMSLOTS;i++) if(g_bm[i].folder<0 && !g_bm[i].nobar) g_slot[g_nslot++]=i;
  for(int f=0;f<g_nbmf && g_nslot<BMSLOTS;f++){ int any=0; for(int i=0;i<g_nbm;i++) if(g_bm[i].folder==f && !g_bm[i].nobar) any=1;
    if(any) g_slot[g_nslot++]=-1-f; }   /* 0.88: a folder whose bookmarks are all hidden is not on the bar */
  char b[64]; int x=0, morew=measure("\xBB"); g_morew=morew;
  g_nshown=0;
  for(int k=0;k<g_nslot;k++){ slot_label(g_slot[k],b,sizeof b); int w=measure(b);
    int room=avail-(k+1<g_nslot?morew+gap:0);
    if(x+w>room) break;
    g_slotx[k]=x; g_slotw[k]=w; x+=w+gap; g_nshown++; }
  g_morex=g_nshown<g_nslot?avail-morew:-1;
  if(g_nshown<g_nslot && g_morex<x-gap) g_morex=x; }

/* menu rows for the Bookmarks submenu / folder / overflow popups: lvl 0, or lvl 1 under a folder row (ud -1) */
#define BMI_MAX 64
#define UD_BM 1000     /* MUI menu user data for bookmark i: UD_BM+i; folder f: UD_BMF+f */
#define UD_BMF 3000
static struct { char label[80]; int ud; int lvl; } g_bmi[BMI_MAX]; static int g_nbmi;
static void bmi_add(const char *l,int ud,int lvl){ if(g_nbmi>=BMI_MAX) return; scopy_u(g_bmi[g_nbmi].label,sizeof g_bmi[0].label,l); g_bmi[g_nbmi].ud=ud; g_bmi[g_nbmi].lvl=lvl; g_nbmi++; }
static void bmi_entry(int i,int lvl){ char b[80]; snprintf(b,sizeof b,"%.60s",g_bm[i].title); bmi_add(b,UD_BM+i,lvl); }
static int g_bmi_bar;   /* building a popup that belongs to the bar: leave out bookmarks hidden from the bar */
static void bmi_folder(int f,int lvl){ for(int i=0;i<g_nbm;i++) if(g_bm[i].folder==f && !(g_bmi_bar&&g_bm[i].nobar)) bmi_entry(i,lvl); }
/* the full Bookmarks menu; nested=1: folders as submenus (MUI 5), 0: folder rows open their own popup */
static void bmi_menu(int nested){
  g_nbmi=0; g_bmi_bar=0;
  bmi_add("Bookmark this page   A-D",M_BOOKMARK,0);
  bmi_add(g_bmbar?"Hide bookmarks bar   A-B":"Show bookmarks bar   A-B",M_BMBAR,0);
  bmi_add("Manage bookmarks     A-O",M_BOOKMARKS,0);
  bmi_add("-",M_NONE,0);
  if(!g_bm_known){ bmi_add("(the list needs the 0.87 engine)",M_NONE,0); return; }
  if(!g_nbm){ bmi_add("(no bookmarks yet)",M_NONE,0); return; }
  for(int f=0;f<g_nbmf;f++){ char b[80]; snprintf(b,sizeof b,"%.30s%s",g_bmf[f],nested?"":" \xBB"); bmi_add(b,nested?-1:UD_BMF+f,0); if(nested) bmi_folder(f,1); }
  for(int i=0;i<g_nbm;i++) if(g_bm[i].folder<0) bmi_entry(i,0);
  if(g_nbmi>=BMI_MAX){ g_nbmi=BMI_MAX-1; bmi_add("More: Manage bookmarks...",M_BOOKMARKS,0); } }
static void bmi_rest(int from,int nested){   /* the bar's "\xBB": the slots that did not fit; nested: folders as submenus */
  g_nbmi=0; g_bmi_bar=1;
  for(int k=from;k<g_nslot;k++){ int s=g_slot[k];
    if(s>=0) bmi_entry(s,0);
    else if(nested){ bmi_add(g_bmf[-1-s],-1,0); bmi_folder(-1-s,1); }
    else { char b[80]; snprintf(b,sizeof b,"%.30s \xBB",g_bmf[-1-s]); bmi_add(b,UD_BMF+(-1-s),0); } } }
static void bmi_onefolder(int f){ g_nbmi=0; g_bmi_bar=1; bmi_folder(f,0); g_bmi_bar=0; if(!g_nbmi) bmi_add("(empty)",M_NONE,0); }

/* own popup menus (GadTools, MUI 3.x, and wherever MUI 5's popup is not used) built from g_bmi */
static void pop_from_bmi(int x,int y){
  pop_close(); g_popkind=1;
  for(int i=0;i<g_nbmi && g_popn<POPMAX;i++){ char a[16];
    if(g_bmi[i].lvl) continue;
    int ud=g_bmi[i].ud;
    if(ud>=UD_BMF){ snprintf(a,sizeof a,"%d",ud-UD_BMF); pop_add(g_bmi[i].label,M_BMFOLDER,a); }
    else if(ud>=UD_BM){ snprintf(a,sizeof a,"%d",ud-UD_BM); pop_add(g_bmi[i].label,M_BMOPEN,a); }
    else pop_add(g_bmi[i].label,ud,NULL); }
  pop_place(x,y,0); pop_draw(); }
static void menu_bm(int x,int y){ bmi_menu(0); pop_from_bmi(x,y); }
static void menu_bm_folder(int f,int x,int y){ if(f<0||f>=g_nbmf) return; bmi_onefolder(f); pop_from_bmi(x,y); }
static void menu_bm_rest(int x,int y){ bmi_rest(g_nshown,0); pop_from_bmi(x,y); }

/* ---- 0.88: the bar's right-click menu (a beta tester): slot k of the bar (k<0: the bar itself / the "\xBB" button) ---- */
enum { BMC_NONE, BMC_OPEN, BMC_NEWTAB, BMC_EDIT, BMC_HIDE, BMC_BARHIDE, BMC_MANAGE };
static void bm_ctx_do(int k,int act){
  int s=k>=0&&k<g_nshown?g_slot[k]:-99999; int isbm=s>=0&&s<g_nbm&&g_bm[s].url, isf=s<0&&s>-99999&&-1-s<g_nbmf;
  CRUMB("bookmarks bar menu: slot %d action %d",k,act);
  switch(act){
  case BMC_OPEN: if(isbm) bm_open(s,0); break;
  case BMC_NEWTAB: if(isbm) bm_open(s,1); break;
  case BMC_EDIT:
    if(isbm && g_bm_v2) send_cmd("bmedit %s",g_bm[s].url);
    else open_page("bookmarks"); break;
  case BMC_HIDE:
    if(!g_bm_v2){ set_status("Hiding one bookmark needs the 0.88 engine: use Hide bookmarks bar, or update the Host"); break; }
    if(isbm){ asb_log("bookmarks bar: hide %s",g_bm[s].url); send_cmd("bmhide %s",g_bm[s].url); }
    else if(isf){ int f=-1-s; asb_log("bookmarks bar: hide folder %s",g_bmf[f]); for(int i=0;i<g_nbm;i++) if(g_bm[i].folder==f && !g_bm[i].nobar) send_cmd("bmhide %s",g_bm[i].url); }
    break;
  case BMC_BARHIDE: if(g_bmbar) bmbar_toggle(); break;
  case BMC_MANAGE: open_page("bookmarks"); break; } }
/* rows of that menu (label, action); kind 0 bookmark, 1 folder, 2 bar */
static int bm_ctx_rows(int kind,const char **lab,int *act,int keys){
  int n=0;
  if(kind==0){ lab[n]="Open"; act[n++]=BMC_OPEN; lab[n]="Open in new tab"; act[n++]=BMC_NEWTAB; lab[n]="Edit..."; act[n++]=BMC_EDIT;
    lab[n]="Hide from bookmarks bar"; act[n++]=BMC_HIDE; lab[n]="-"; act[n++]=BMC_NONE; }
  if(kind==1){ lab[n]="Hide folder from bookmarks bar"; act[n++]=BMC_HIDE; lab[n]="-"; act[n++]=BMC_NONE; }
  lab[n]=keys?"Hide bookmarks bar   A-B":"Hide bookmarks bar"; act[n++]=BMC_BARHIDE; lab[n]=keys?"Manage bookmarks     A-O":"Manage bookmarks"; act[n++]=BMC_MANAGE;
  return n; }
static int bm_slot_kind(int k){ if(k<0||k>=g_nshown) return 2; return g_slot[k]>=0?0:1; }

/* ---- 0.88: favicons on the bar (a beta tester). A 0.88 engine answers "favicon HOST" with ASBC_FAVICON (16x16 RGBA, about
   1 KB once per site; see chrome/favicon.inc). They are kept in PROGDIR:Data/Favicons/ (HOST.rgba, or HOST.none
   when the site has none: asked again after 30 days), so a site is fetched over the link only once. Bookmarks
   without one show PROGDIR:Icons/favicon.png, folders PROGDIR:Icons/folder16.png. ---- */
#define FAV_DIR "PROGDIR:Data/Favicons"
#define FAVMAX 96
static struct { char host[64]; unsigned char *px; int state; } g_fv[FAVMAX];   /* state 1 asked, 2 have, 3 none */
static int g_nfv; static unsigned char *g_fv_def, *g_fv_folder; static int g_fv_loaded;
static void fav_host(const char *u,char *o,int cap){
  o[0]=0; const char *h=strstr(u,"://"); if(!h || (strncasecmp(u,"http",4))) return; h+=3;
  const char *e=h; while(*e && *e!='/' && *e!='?' && *e!='#') e++;
  const char *at=h; for(const char *q=h;q<e;q++) if(*q=='@') at=q+1;
  int n=0; for(const char *q=at;q<e && n<cap-1;q++){ char c=*q; if(c>='A'&&c<='Z') c=(char)(c+32);
    if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c==':')){ o[0]=0; return; } o[n++]=c; }
  o[n]=0; }
static void fav_file(char *o,int cap,const char *host,const char *ext){
  char h[64]; scopy_u(h,sizeof h,host); for(char *p=h;*p;p++) if(*p==':') *p='_';
  snprintf(o,(size_t)cap,FAV_DIR "/%s%s",h,ext); }
static int fav_old(const char *path){   /* the file is older than 30 days */
  BPTR l=Lock((STRPTR)path,ACCESS_READ); if(!l) return 0;
  struct FileInfoBlock *fib=AllocDosObject(DOS_FIB,NULL); int old=0;
  if(fib){ if(Examine(l,fib)){ struct DateStamp now; DateStamp(&now); old=now.ds_Days-fib->fib_Date.ds_Days>30; } FreeDosObject(DOS_FIB,fib); }
  UnLock(l); return old; }
static int fav_find(const char *host){ for(int i=0;i<g_nfv;i++) if(!strcmp(g_fv[i].host,host)) return i; return -1; }
static void fav_defaults(void){
  if(g_fv_loaded) return; g_fv_loaded=1; int w,h;
  g_fv_def=load_png_file("PROGDIR:Icons/favicon.png",&w,&h); if(g_fv_def&&(w!=16||h!=16)){ free(g_fv_def); g_fv_def=NULL; }
  g_fv_folder=load_png_file("PROGDIR:Icons/folder16.png",&w,&h); if(g_fv_folder&&(w!=16||h!=16)){ free(g_fv_folder); g_fv_folder=NULL; } }
/* icon for bookmark i (never NULL unless the default is missing); asks the engine the first time */
static const unsigned char *fav_for(int i){
  fav_defaults();
  if(i<0||i>=g_nbm||!g_bm[i].url) return g_fv_def;
  char host[64]; fav_host(g_bm[i].url,host,sizeof host); if(!host[0]) return g_fv_def;
  int k=fav_find(host);
  if(k<0){ if(g_nfv>=FAVMAX) return g_fv_def; k=g_nfv++; scopy_u(g_fv[k].host,sizeof g_fv[0].host,host); g_fv[k].px=NULL; g_fv[k].state=0;
    char p[120]; fav_file(p,sizeof p,host,".rgba");
    FILE *f=fopen(p,"rb"); if(f){ unsigned char *b=malloc(1024); if(b && fread(b,1,1024,f)==1024){ g_fv[k].px=b; g_fv[k].state=2; } else free(b); fclose(f); }
    if(!g_fv[k].state){ fav_file(p,sizeof p,host,".none"); BPTR l=Lock((STRPTR)p,ACCESS_READ);
      if(l){ UnLock(l); if(fav_old(p)) DeleteFile((STRPTR)p); else g_fv[k].state=3; } } }
  if(!g_fv[k].state && g_bm_v2 && g_fd>=0){ g_fv[k].state=1; send_cmd("favicon %s",host); }
  return g_fv[k].state==2?g_fv[k].px:g_fv_def; }
static void fav_recv(const unsigned char *p,int n){   /* ASBC_FAVICON */
  if(n<3) return; int hl=p[0]; if(n<1+hl+2) return;
  char host[64]; int m=hl<63?hl:63; memcpy(host,p+1,(size_t)m); host[m]=0;
  int w=p[1+hl], h=p[2+hl]; const unsigned char *px=p+3+hl;
  int k=fav_find(host); if(k<0){ if(g_nfv>=FAVMAX) return; k=g_nfv++; scopy_u(g_fv[k].host,sizeof g_fv[0].host,host); g_fv[k].px=NULL; }
  data_dir(); { BPTR l=Lock((STRPTR)FAV_DIR,ACCESS_READ); if(!l) l=CreateDir((STRPTR)FAV_DIR); if(l) UnLock(l); }
  char path[120];
  if(w==16 && h==16 && n>=3+hl+1024){
    unsigned char *b=malloc(1024); if(!b) return; memcpy(b,px,1024); free(g_fv[k].px); g_fv[k].px=b; g_fv[k].state=2;
    fav_file(path,sizeof path,host,".rgba"); FILE *f=fopen(path,"wb"); if(f){ fwrite(b,1,1024,f); fclose(f); } }
  else { g_fv[k].state=3; fav_file(path,sizeof path,host,".none"); FILE *f=fopen(path,"w"); if(f) fclose(f); }
  asb_log("favicon %s: %s",host,g_fv[k].state==2?"saved":"none (default icon)");
  g_bm_dirty=1; }
static void fav_free(void){ for(int i=0;i<g_nfv;i++){ free(g_fv[i].px); g_fv[i].px=NULL; } g_nfv=0; free(g_fv_def); free(g_fv_folder); g_fv_def=g_fv_folder=NULL; g_fv_loaded=0; }
/* draw a 16x16 RGBA icon at x,y (OX/OY space). bg>=0: blend over that theme colour (the GadTools bar);
   bg<0: unknown background (a MUI button): pixels with enough alpha only, in runs */
static void fav_draw(const unsigned char *px,int x,int y,int bg){
  if(!px||!can_rgb()) return;
  unsigned char row[16*3];
  ULONG b=bg>=0?trgb[bg]:trgb[P_CHROME]; int br=(int)(b>>16&255), bgc=(int)(b>>8&255), bb=(int)(b&255);
  for(int j=0;j<16;j++){ const unsigned char *s=px+j*64;
    for(int i=0;i<16;i++){ int a=s[i*4+3]; row[i*3]=(unsigned char)((s[i*4]*a+br*(255-a))/255); row[i*3+1]=(unsigned char)((s[i*4+1]*a+bgc*(255-a))/255); row[i*3+2]=(unsigned char)((s[i*4+2]*a+bb*(255-a))/255); }
    if(bg>=0){ put_rgb(row,16*3,x,y+j,16,1); continue; }
    for(int i=0;i<16;){ if(s[i*4+3]<100){ i++; continue; } int i0=i; while(i<16 && s[i*4+3]>=100) i++; put_rgb(row+i0*3,16*3,x+i0,y+j,i-i0,1); } } }
static const unsigned char *fav_slot(int k){   /* icon of bar slot k */
  if(k<0||k>=g_nshown) return NULL; int s=g_slot[k]; fav_defaults(); return s>=0?fav_for(s):g_fv_folder; }
