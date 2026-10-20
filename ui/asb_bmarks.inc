/* ---- 0.87: bookmarks bar and Bookmarks menu (all three programs) ----
   The bookmarks stay where they always were, in the engine's bookmarks.tsv on the PC (same format; 0.87 adds an
   optional 4th column, the folder, set on the Manage bookmarks page). After connecting AmiSurf asks for the list
   ("bookmarks"); an 0.87+ engine answers with ASBC_BMARKS and sends it again whenever it changes. An older engine
   does not answer: the bar then shows one "Bookmarks" button (the bookmarks page) and the menu says why.
   The bar: bookmarks without a folder first, then one button per folder, then a "\xBB" button with whatever did
   not fit. Click opens; Shift-click (and middle-click in the GadTools build) opens in a new tab.
   BOOKMARKSBAR=ON|OFF (Data/AmiSurf.prefs or tooltype), Amiga-B toggles it. */
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
static struct { char title[48]; char *url; int folder; } g_bm[BMMAX];   /* folder: -1 none, else index in g_bmf */
static char g_bmf[BMFMAX][32]; static int g_nbm, g_nbmf;
static int g_bm_known;            /* the engine sent a list (0.87+ engine) */
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
  bm_free(); g_bm_known=1;
  const unsigned char *e=p+n;
  while(p<e && g_nbm<BMMAX){
    const unsigned char *nl=p; while(nl<e && *nl!='\n') nl++;
    const unsigned char *t1=p; while(t1<nl && *t1!='\t') t1++;
    const unsigned char *t2=t1<nl?t1+1:nl; while(t2<nl && *t2!='\t') t2++;
    int ul=(int)(t1-p);
    if(ul>0){
      char *u=malloc((size_t)ul+1); if(!u) break; memcpy(u,p,(size_t)ul); u[ul]=0;
      char tt[48]; if(t1<nl) u8_l1(tt,sizeof tt,(const char*)t1+1,(int)(t2-t1-1)); else tt[0]=0;
      char fo[32]=""; if(t2<nl) u8_l1(fo,sizeof fo,(const char*)t2+1,(int)(nl-t2-1));
      int f=-1; if(fo[0]){ for(int k=0;k<g_nbmf;k++) if(!strcmp(g_bmf[k],fo)) f=k;
        if(f<0 && g_nbmf<BMFMAX){ f=g_nbmf++; scopy_u(g_bmf[f],sizeof g_bmf[0],fo); } }
      g_bm[g_nbm].url=u; g_bm[g_nbm].folder=f;
      if(tt[0]) scopy_u(g_bm[g_nbm].title,sizeof g_bm[0].title,tt);
      else { const char *h=strstr(u,"://"); h=h?h+3:u; scopy_u(g_bm[g_nbm].title,sizeof g_bm[0].title,h); }
      g_nbm++; }
    p=nl+1; }
  asb_log("bookmarks: %d (%d folders) from the engine",g_nbm,g_nbmf);
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
  for(int i=0;i<g_nbm && g_nslot<BMSLOTS;i++) if(g_bm[i].folder<0) g_slot[g_nslot++]=i;
  for(int f=0;f<g_nbmf && g_nslot<BMSLOTS;f++) g_slot[g_nslot++]=-1-f;
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
static void bmi_folder(int f,int lvl){ for(int i=0;i<g_nbm;i++) if(g_bm[i].folder==f) bmi_entry(i,lvl); }
/* the full Bookmarks menu; nested=1: folders as submenus (MUI 5), 0: folder rows open their own popup */
static void bmi_menu(int nested){
  g_nbmi=0;
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
  g_nbmi=0;
  for(int k=from;k<g_nslot;k++){ int s=g_slot[k];
    if(s>=0) bmi_entry(s,0);
    else if(nested){ bmi_add(g_bmf[-1-s],-1,0); bmi_folder(-1-s,1); }
    else { char b[80]; snprintf(b,sizeof b,"%.30s \xBB",g_bmf[-1-s]); bmi_add(b,UD_BMF+(-1-s),0); } } }
static void bmi_onefolder(int f){ g_nbmi=0; bmi_folder(f,0); if(!g_nbmi) bmi_add("(empty)",M_NONE,0); }

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
