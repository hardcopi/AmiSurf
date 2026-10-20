/* asb_theme.inc — AmiStudio Paint look for the 68k client (included by asb_gfx_chrome.c).
   Themes: default = the user's system/MUI prefs (DrawInfo pens, Workbench font);
           THEME=PRO = Paint's dark theme (chrome #535353, dock #424242, field #454545, text #EEEEEE,
           accent #378EF0, page background #282828).
   Fonts:  Workbench screen font; SCREENFONT=TOPAZ = ROM topaz 8.
   Screen: SCREENMODE=<hex id> | SCREENMODE=<W>x<H> | SCREENMODE=WB, MODEREQ (ASL requester), else Paint's automatic RTG pick
           (1920x1080 ... 640x480, 15-bit or deeper), else AGA 256 colours (6x6x6 cube + 24 greys, Bayer dither).
   Icons:  24x24 RGBA PNG from PROGDIR:Icons/<name>.png; grey icons are masks tinted with the theme text colour.
   No floating point anywhere: the one 68020 binary must stay fast on 040/060. */

#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <libraries/asl.h>
#include <proto/asl.h>
#include <graphics/text.h>
#include <proto/diskfont.h>

enum { P_CHROME, P_TEXT, P_BORDER, P_TAB, P_SHADOW, P_DOCK, P_FIELD, P_ACCENT, P_SEP, P_HOVER,
       P_PRESSED, P_DIM, P_FAINT, P_ROWSEL, P_CARDB, P_CANVAS, P_NUM };
static const ULONG pro_rgb[P_NUM]={0x535353,0xEEEEEE,0x686868,0x4A4A4A,0x282828,0x424242,0x454545,0x378EF0,
                                   0x3E3E3E,0x626262,0x707070,0xD4D4D4,0xA0A0A0,0x6B6B6B,0x424242,0x282828};
static LONG tp[P_NUM];          /* pen per role */
static ULONG trgb[P_NUM];       /* 0xRRGGBB per role (for icon compositing) */
static int  tp_obtained[P_NUM];
static int  g_frames_qoi, g_stats; /* FRAMES=QOI: old decode path; STATS: speed numbers in the status bar */
static int  g_debug, g_theme_pro, g_font_topaz, g_modereq, g_aga, g_ownscr, g_splash_ms=1500;
static ULONG g_modeid=INVALID_ID; static int g_wbmode=1, g_modew, g_modeh; /* 0.72: a Workbench window is the default */
static int g_wx=-1, g_wy=-1, g_ww, g_wh;  /* remembered window box (WINDOW=x,y,w,h), -1/0 = default */
static int g_clut; static LONG g_cube[64]; static int g_cube_n; /* 8-bit (AGA or RTG CLUT) Workbench: dither onto obtained pens */
static int g_autohost=-1; static char g_hostcmd[400]; /* AUTOHOST=ON|OFF (default: on when C:host-run exists), HOSTCMD= override */
static int g_browser; /* BROWSER=: 0 host default, 1 Chrome, 2 Firefox (sent in HELLO) */
static struct TextFont *g_font;
static char g_uifont[48]; static int g_uisize, g_large;
struct Library *AslBase, *DiskfontBase;
static struct TextAttr topaz8={(STRPTR)"topaz.font",8,0,FPF_ROMFONT};

static void theme_tooltype(const char *k,const char *v){
  if(!strcasecmp(k,"THEME")) g_theme_pro=v&&!strcasecmp(v,"PRO");
  else if(!strcasecmp(k,"SCREENFONT")) g_font_topaz=v&&!strncasecmp(v,"TOPAZ",5);
  else if(!strcasecmp(k,"MODEREQ")){ g_modereq=1; g_wbmode=0; }
  else if(!strcasecmp(k,"WINDOW")&&v){ int a[4]={-1,-1,0,0},n=0; const char *q=v;
    while(n<4&&*q){ a[n++]=atoi(q); while(*q&&*q!=',') q++; if(*q) q++; }
    if(n==4&&a[2]>=200&&a[3]>=120){ g_wx=a[0]; g_wy=a[1]; g_ww=a[2]; g_wh=a[3]; } }
  else if(!strcasecmp(k,"DEBUG")) g_debug=1;
  else if(!strcasecmp(k,"HOST")&&v) snprintf(g_host,sizeof g_host,"%s",v);   /* host[:port] of the AmiStudio Host */
  else if(!strcasecmp(k,"HOSTMODE")&&v){ g_legacy=!strcasecmp(v,"LEGACY")||!strcasecmp(v,"ASBC"); g_legacy_set=1; }
  else if(!strcasecmp(k,"SERVICE")&&v&&v[0]) snprintf(g_service,sizeof g_service,"%s",v);
  else if(!strcasecmp(k,"AUTOHOST")) g_autohost=!v||!(!strcasecmp(v,"OFF")||!strcasecmp(v,"NO")||!strcmp(v,"0"));
  else if(!strcasecmp(k,"HOSTCMD")&&v) snprintf(g_hostcmd,sizeof g_hostcmd,"%s",v);
  else if(!strcasecmp(k,"FRAMES")) g_frames_qoi=v&&!strcasecmp(v,"QOI");
  else if(!strcasecmp(k,"LOG")){ if(v&&(!strcasecmp(v,"OFF")||!strcmp(v,"0")||!strcasecmp(v,"NO"))) g_log=0; else if(v&&v[0]&&strcasecmp(v,"ON")) snprintf(g_logpath,sizeof g_logpath,"%s",v); }
  else if(!strcasecmp(k,"SOUND")){ g_sound=!v||!(!strcasecmp(v,"OFF")||!strcasecmp(v,"NO")||!strcmp(v,"0")); g_sound_adpcm=v&&!strcasecmp(v,"ADPCM"); }
  else if(!strcasecmp(k,"BOOKMARKSBAR")){ g_bmbar=!(v&&(!strcasecmp(v,"OFF")||!strcasecmp(v,"NO")||!strcmp(v,"0"))); }
  else if(!strcasecmp(k,"AVSYNC")){ if(v&&(!strcasecmp(v,"OFF")||!strcasecmp(v,"NO"))) g_avsync_off=1; else if(v&&v[0]){ g_avsync_off=0; avsync_set(atoi(v)); } }
  else if(!strcasecmp(k,"STATS")) g_stats=!v||strcasecmp(v,"OFF")!=0;
  else if(!strcasecmp(k,"UIFONT")&&v){ /* UIFONT=helvetica.font/13 or helvetica/13 */
    char n[48]; snprintf(n,sizeof n,"%s",v); char *sl=strchr(n,'/'); g_uisize=sl?atoi(sl+1):0; if(sl) *sl=0;
    if(!strstr(n,".font")) strncat(n,".font",sizeof n-strlen(n)-1); snprintf(g_uifont,sizeof g_uifont,"%s",n); if(g_uisize<6||g_uisize>40) g_uisize=13; }
  else if(!strcasecmp(k,"TOOLBAR")&&v) g_large=!strcasecmp(v,"LARGE")||!strcasecmp(v,"BIG");
  else if(!strcasecmp(k,"BIGUI")) g_large=!v||atoi(v)!=0;
  else if(!strcasecmp(k,"SPLASHHOLD")&&v) g_splash_ms=atoi(v);
  else if(!strcasecmp(k,"BROWSER")&&v) g_browser=!strncasecmp(v,"FIREFOX",7)?2:!strncasecmp(v,"CHROME",6)?1:0;
  else if(!strcasecmp(k,"SCREENMODE")&&v){
    if(!strcasecmp(v,"WB")||!strcasecmp(v,"WORKBENCH")) g_wbmode=1;
    else if(!strcasecmp(v,"AUTO")||!strncasecmp(v,"FULL",4)){ g_wbmode=0; g_modeid=INVALID_ID; }
    else if(v[0]!='0'&&strchr(v,'x')){ g_wbmode=0; g_modew=atoi(v); g_modeh=atoi(strchr(v,'x')+1); } /* resolved in screen_open */
    else { char *e; unsigned long id=strtoul(v,&e,16); if(e!=v){ g_modeid=(ULONG)id; g_wbmode=0; } } }
}

/* ---- screen mode (Paint's logic) ---- */
static int mode_ok(ULONG id,int w,int h,int mindepth){
  struct DimensionInfo di;
  if(id==INVALID_ID) return 0;
  if(!GetDisplayInfoData(NULL,(UBYTE*)&di,sizeof di,DTAG_DIMS,id)) return 0;
  if(di.MaxDepth<mindepth) return 0;
  if(w && (di.Nominal.MaxX-di.Nominal.MinX+1!=w || di.Nominal.MaxY-di.Nominal.MinY+1!=h)) return 0;
  return 1; }
static ULONG pick_rtg_mode(void){
  /* first choice: the Workbench's own mode when it is a 15+ bit RTG mode (known to work on this monitor) */
  { struct Screen *wb=LockPubScreen(NULL);
    if(wb){ ULONG id=GetVPModeID(&wb->ViewPort); int d=wb->RastPort.BitMap?GetBitMapAttr(wb->RastPort.BitMap,BMA_DEPTH):0; UnlockPubScreen(NULL,wb);
      if(id!=INVALID_ID && d>=15 && mode_ok(id,0,0,15)) return id; } }
  static const short sz[13][2]={{1920,1080},{1680,1050},{1600,1200},{1600,900},{1440,900},{1366,768},{1280,1024},
                                {1280,960},{1280,800},{1280,720},{1024,768},{800,600},{640,480}};
  static const short dp[3]={24,16,15};
  for(int i=0;i<13;i++) for(int d=0;d<3;d++){
    ULONG id=BestModeID(BIDTAG_NominalWidth,sz[i][0],BIDTAG_NominalHeight,sz[i][1],BIDTAG_Depth,dp[d],TAG_DONE);
    if(mode_ok(id,sz[i][0],sz[i][1],15)) return id; }
  /* largest 15+ bit mode of at least 800x600 */
  ULONG id=INVALID_ID, best=INVALID_ID; long barea=0;
  while((id=NextDisplayInfo(id))!=INVALID_ID){
    struct DimensionInfo di;
    if(!GetDisplayInfoData(NULL,(UBYTE*)&di,sizeof di,DTAG_DIMS,id) || di.MaxDepth<15) continue;
    long w=di.Nominal.MaxX-di.Nominal.MinX+1, h=di.Nominal.MaxY-di.Nominal.MinY+1;
    if(w>=800 && h>=600 && w*h>barea){ barea=w*h; best=id; } }
  return best; }
static ULONG ask_mode(void){
  ULONG id=INVALID_ID;
  if(!AslBase) AslBase=OpenLibrary((STRPTR)"asl.library",38);
  if(!AslBase) return id;
  struct ScreenModeRequester *r=(struct ScreenModeRequester*)AllocAslRequestTags(ASL_ScreenModeRequest,
      ASLSM_TitleText,(ULONG)"AmiSurf screen mode",ASLSM_DoDepth,FALSE,TAG_DONE);
  if(r){ if(AslRequestTags(r,TAG_DONE)) id=r->sm_DisplayID; FreeAslRequest(r); }
  return id; }

/* AGA: pens 0-15 theme, 16-231 6x6x6 cube, 232-255 greys */
static void aga_palette(struct ViewPort *vp){
  for(int i=0;i<P_NUM;i++){ ULONG c=g_theme_pro?pro_rgb[i]:0; 
    if(!g_theme_pro){ static const ULONG lite[P_NUM]={0xAAAAAA,0x000000,0x777777,0x999999,0x555555,0x9A9A9A,0xFFFFFF,0x6688BB,
                                                      0x888888,0xBBBBBB,0x888888,0x333333,0x555555,0xCCCCCC,0x999999,0xFFFFFF}; c=lite[i]; }
    SetRGB32(vp,i,(c>>16&255)*0x01010101UL,(c>>8&255)*0x01010101UL,(c&255)*0x01010101UL); }
  for(int r=0;r<6;r++) for(int g=0;g<6;g++) for(int b=0;b<6;b++)
    SetRGB32(vp,16+r*36+g*6+b,(ULONG)(r*51)*0x01010101UL,(ULONG)(g*51)*0x01010101UL,(ULONG)(b*51)*0x01010101UL);
  for(int i=0;i<24;i++){ ULONG v=(ULONG)(8+i*10)*0x01010101UL; SetRGB32(vp,232+i,v,v,v); }
}

/* open our own screen (RTG or AGA) unless Workbench mode; returns 0 on success */
static int screen_open(void){
  if(g_wbmode) return -1;
  ULONG id=g_modeid;
  if(id==INVALID_ID && g_modew>0 && g_modeh>0){ /* SCREENMODE=<W>x<H>: exact 15+ bit mode, else the closest in size */
    ULONG m=INVALID_ID; long bd=0x7fffffffL; int bdep=0;
    while((m=NextDisplayInfo(m))!=INVALID_ID){
      struct DimensionInfo di;
      if(!GetDisplayInfoData(NULL,(UBYTE*)&di,sizeof di,DTAG_DIMS,m) || di.MaxDepth<15) continue;
      long w=di.Nominal.MaxX-di.Nominal.MinX+1, h=di.Nominal.MaxY-di.Nominal.MinY+1;
      long d=(w>g_modew?w-g_modew:g_modew-w)+(h>g_modeh?h-g_modeh:g_modeh-h);
      if(d<bd || (d==bd && di.MaxDepth>bdep)){ bd=d; bdep=di.MaxDepth; id=m; } } }
  if(g_modereq){ ULONG a=ask_mode(); if(a!=INVALID_ID) id=a; }
  if(id==INVALID_ID || !mode_ok(id,0,0,8)) id=pick_rtg_mode();
  int depth=24;
  if(id!=INVALID_ID){ struct DimensionInfo di; GetDisplayInfoData(NULL,(UBYTE*)&di,sizeof di,DTAG_DIMS,id); depth=di.MaxDepth>24?24:di.MaxDepth; }
  if(id==INVALID_ID || depth<15){
    id=BestModeID(BIDTAG_NominalWidth,640,BIDTAG_NominalHeight,512,BIDTAG_Depth,8,TAG_DONE);
    if(id==INVALID_ID) id=PAL_MONITOR_ID|HIRESLACE_KEY;
    depth=8; g_aga=1; }
  struct TextAttr *ta=g_font_topaz?&topaz8:NULL;
  /* size the screen to the mode's visible area: SA_LikeWorkbench alone copies the Workbench's size, and a
     Workbench larger than this mode gave an autoscroll screen whose page was wider than what is shown */
  LONG sw=0,sh=0;
  { struct DimensionInfo di; if(GetDisplayInfoData(NULL,(UBYTE*)&di,sizeof di,DTAG_DIMS,id)){
      sw=di.Nominal.MaxX-di.Nominal.MinX+1; sh=di.Nominal.MaxY-di.Nominal.MinY+1; } }
  struct Screen *s=OpenScreenTags(NULL,SA_DisplayID,id,SA_Depth,depth,SA_LikeWorkbench,TRUE,
      sw>0?SA_Width:TAG_IGNORE,sw,sh>0?SA_Height:TAG_IGNORE,sh,SA_AutoScroll,FALSE,
      SA_Title,(ULONG)"AmiSurf",SA_ShowTitle,FALSE,SA_Quiet,TRUE,SA_Type,CUSTOMSCREEN,
      SA_SharePens,TRUE,SA_Interleaved,TRUE,ta?SA_Font:TAG_IGNORE,(ULONG)ta,TAG_DONE);
  if(!s){ g_aga=0; return -1; }
  scr=s; g_ownscr=1;
  if(g_aga) aga_palette(&s->ViewPort);
  return 0; }

/* Workbench window box: the remembered one if it still fits, else 90% of the screen, centred under the title bar */
static void win_box(struct Screen *s,int *x,int *y,int *w,int *h){
  int bar=s->BarHeight+1, sw=s->Width, sh=s->Height-bar;
  if(g_ww>=200 && g_wh>=120 && g_wx>=0 && g_wy>=0){
    *w=g_ww>sw?sw:g_ww; *h=g_wh>sh?sh:g_wh; *x=g_wx; *y=g_wy;
    if(*x+*w>sw) *x=sw-*w; if(*y+*h>s->Height) *y=s->Height-*h; if(*y<bar) *y=bar; return; }
  *w=sw*9/10; *h=sh*9/10; if(*w<200) *w=sw; if(*h<120) *h=sh;
  *x=(sw-*w)/2; *y=bar+(sh-*h)/2; }
static void win_box_save(struct Window *w){
  if(!w||g_ownscr) return;
  char b[64]; snprintf(b,sizeof b,"%d,%d,%d,%d\n",w->LeftEdge,w->TopEdge,w->Width,w->Height);
  data_dir(); FILE *f=fopen(WIN_FILE,"w"); if(f){ fputs(b,f); fclose(f); } }   /* 0.86: in the drawer */
static void win_box_load(void){
  FILE *f=fopen(WIN_FILE,"r");
  if(!f) return;
  char b[64]; if(fgets(b,sizeof b,f)){ b[strcspn(b,"\r\n")]=0; theme_tooltype("WINDOW",b); } fclose(f); }
/* 8-bit Workbench (AGA, or an RTG CLUT mode): share a 4x4x4 colour cube from the Workbench palette */
static void clut_init(void){
  if(g_cube_n) return; g_clut=0; if(g_ownscr||g_aga||!scr) return;
  int d=scr->RastPort.BitMap?(int)GetBitMapAttr(scr->RastPort.BitMap,BMA_DEPTH):8; if(d>8) return;
  struct ColorMap *cm=scr->ViewPort.ColorMap; if(!cm) return;
  for(int r=0;r<4;r++) for(int g=0;g<4;g++) for(int b=0;b<4;b++){
    LONG p=ObtainBestPen(cm,(ULONG)(r*85)*0x01010101UL,(ULONG)(g*85)*0x01010101UL,(ULONG)(b*85)*0x01010101UL,OBP_Precision,PRECISION_IMAGE,TAG_DONE);
    g_cube[r*16+g*4+b]=p<0?0:p; }
  g_cube_n=64; g_clut=1; g_rtg=0; }
static void clut_free(void){
  struct ColorMap *cm=scr?scr->ViewPort.ColorMap:NULL;
  if(cm) for(int i=0;i<g_cube_n;i++) ReleasePen(cm,(ULONG)g_cube[i]); g_cube_n=0; }

/* ---- pens ---- */
static ULONG pen_rgb(LONG pen){
  ULONG c[3]; GetRGB32(scr->ViewPort.ColorMap,(ULONG)pen,1,c);
  return (c[0]>>24<<16)|(c[1]>>24<<8)|(c[2]>>24); }
static void theme_pens(void){
  struct ColorMap *cm=scr->ViewPort.ColorMap;
  struct DrawInfo *dri=GetScreenDrawInfo(scr);
  for(int i=0;i<P_NUM;i++){
    tp_obtained[i]=0;
    if(g_aga){ tp[i]=i; continue; }
    if(!g_theme_pro && dri){
      UWORD *dp=dri->dri_Pens; LONG p;
      switch(i){
        case P_TEXT: case P_DIM: p=dp[TEXTPEN]; break;
        case P_FAINT: p=dp[SHADOWPEN]; break;
        case P_ACCENT: p=dp[FILLPEN]; break;
        case P_FIELD: case P_CANVAS: p=dp[SHINEPEN]; break;
        case P_BORDER: case P_SEP: case P_SHADOW: p=dp[SHADOWPEN]; break;
        case P_PRESSED: case P_ROWSEL: p=dp[FILLPEN]; break;
        default: p=dp[BACKGROUNDPEN]; }
      tp[i]=p; continue; }
    ULONG c=pro_rgb[i];
    LONG p=cm?ObtainBestPen(cm,(c>>16&255)*0x01010101UL,(c>>8&255)*0x01010101UL,(c&255)*0x01010101UL,OBP_Precision,PRECISION_EXACT,TAG_DONE):-1;
    if(p<0) p=(i==P_TEXT)?1:0; else tp_obtained[i]=1;
    tp[i]=p; }
  if(!g_theme_pro && dri){ /* tab strip a shade darker than the chrome when the palette has one */
    tp[P_TAB]=tp[P_CHROME]; }
  if(dri) FreeScreenDrawInfo(scr,dri);
  for(int i=0;i<P_NUM;i++) trgb[i]=g_aga?(g_theme_pro?pro_rgb[i]:0):pen_rgb(tp[i]);
  if(g_aga) for(int i=0;i<P_NUM;i++) trgb[i]=pen_rgb(i);
  pen_bg=tp[P_CHROME]; pen_fg=tp[P_TEXT]; pen_face=tp[P_FIELD]; pen_line=tp[P_BORDER];
  pens_got=1; }
static void theme_pens_free(void){
  struct ColorMap *cm=scr?scr->ViewPort.ColorMap:NULL;
  for(int i=0;i<P_NUM;i++) if(tp_obtained[i]&&cm){ ReleasePen(cm,tp[i]); tp_obtained[i]=0; }
  clut_free(); pens_got=0; }

/* ---- font ---- */
static void theme_font(void){
  if(g_uifont[0]){ struct TextAttr ta={(STRPTR)g_uifont,(UWORD)g_uisize,0,0};
    g_font=OpenFont(&ta);
    if(!g_font || g_font->tf_YSize!=g_uisize){ if(g_font) CloseFont(g_font); g_font=NULL;
      if(!DiskfontBase) DiskfontBase=OpenLibrary((STRPTR)"diskfont.library",36); if(DiskfontBase) g_font=OpenDiskFont(&ta); } }
  if(g_font){}
  else if(g_font_topaz) g_font=OpenFont(&topaz8);
  else if(scr->Font){
    g_font=OpenFont(scr->Font);
    if(!g_font){ if(!DiskfontBase) DiskfontBase=OpenLibrary((STRPTR)"diskfont.library",36); if(DiskfontBase) g_font=OpenDiskFont(scr->Font); } }
  if(g_font && RP) SetFont(RP,g_font); }

/* ---- RGB output: RTG direct, AGA ordered dither into the cube ---- */
static const UBYTE bayer4[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
static UBYTE *g_chunky; static int g_chunky_n;
static void put_rgb(const unsigned char *pix,int bpr,int x,int y,int w,int h){
  if(w<1||h<1) return;
  if(P96Base && g_rtg){
    struct asb_p96_renderinfo ri; ri.Memory=(APTR)pix; ri.BytesPerRow=(WORD)bpr; ri.pad=0; ri.RGBFormat=ASB_RGBFB_R8G8B8;
    asb_p96WritePixelArray(&ri,0,0,RP,(UWORD)(OX+x),(UWORD)(OY+y),(UWORD)w,(UWORD)h); return; }
  if(CyberGfxBase && g_rtg){
    asb_cgxWritePixelArray((APTR)pix,0,0,(UWORD)bpr,RP,(UWORD)(OX+x),(UWORD)(OY+y),(UWORD)w,(UWORD)h,ASB_RECTFMT_RGB); return; }
  if(!g_aga && !g_clut) return;
  if(g_chunky_n<w){ free(g_chunky); g_chunky=malloc((size_t)w); g_chunky_n=g_chunky?w:0; if(!g_chunky) return; }
  for(int j=0;j<h;j++){
    const unsigned char *s=pix+j*bpr; const UBYTE *bm=bayer4+(((y+j)&3)<<2);
    for(int i=0;i<w;i++,s+=3){
      if(g_clut){ int t=bm[(x+i)&3]*5; /* 0..75 of an 85 step */
        int r=(s[0]+t)/85, g=(s[1]+t)/85, b=(s[2]+t)/85; if(r>3) r=3; if(g>3) g=3; if(b>3) b=3;
        g_chunky[i]=(UBYTE)g_cube[r*16+g*4+b]; continue; }
      int t=bm[(x+i)&3]*3; /* 0..45 of a 51 step */
      int r=s[0]+t, g=s[1]+t, b=s[2]+t;
      r=r>=255?5:r/51; g=g>=255?5:g/51; b=b>=255?5:b/51;
      if(r>5) r=5; if(g>5) g=5; if(b>5) b=5;
      g_chunky[i]=(UBYTE)(16+r*36+g*6+b); }
    WriteChunkyPixels(RP,(ULONG)(OX+x),(ULONG)(OY+y+j),(ULONG)(OX+x+w-1),(ULONG)(OY+y+j),g_chunky,(LONG)w); } }
static int can_rgb(void){ return g_rtg || g_aga || g_clut; }

/* ---- icons ---- */
#define IC_SZ 24
enum { IC_BACK, IC_FWD, IC_RELOAD, IC_STOP, IC_HOME, IC_STAR, IC_BOOKMARKS, IC_HISTORY, IC_FIND, IC_ABOUT, IC_PLUS, IC_CLOSE,
       IC_MENU, IC_LOCK, IC_UNLOCK, IC_ZOOMIN, IC_ZOOMOUT, IC_DOWNLOAD, IC_NUM };
static const char *ic_name[IC_NUM]={"back","fwd","reload","stop","home","star","bookmarks","history","find","info","plus","close","menu","lock","unlock","zoomin","zoomout","download"};
static const char *ic_label[IC_NUM]={"<",">","R","X","H","*","B","Y","F","?","+","x","=","L","!","+","-","D"};
static unsigned char *ic_px[IC_NUM]; static int ic_grey[IC_NUM];
static unsigned char *load_png_file(const char *path,int *w,int *h){
  FILE *f=fopen(path,"rb"); if(!f) return NULL;
  fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
  unsigned char *b=(n>0&&n<4*1024*1024)?malloc((size_t)n):NULL;
  if(!b||fread(b,1,(size_t)n,f)!=(size_t)n){ fclose(f); free(b); return NULL; }
  fclose(f); int c; unsigned char *px=stbi_load_from_memory(b,(int)n,w,h,&c,4); free(b); return px; }
static void icons_load(void){
  for(int i=0;i<IC_NUM;i++){
    char p[80]; snprintf(p,sizeof p,"PROGDIR:Icons/%s.png",ic_name[i]);
    int w=0,h=0; unsigned char *px=load_png_file(p,&w,&h);
    if(px && (w!=IC_SZ||h!=IC_SZ)){ free(px); px=NULL; }
    ic_px[i]=px; ic_grey[i]=1;
    if(px) for(int k=0;k<IC_SZ*IC_SZ;k++){ unsigned char *q=px+k*4; if(q[3]>16 && (abs(q[0]-q[1])>12||abs(q[1]-q[2])>12)){ ic_grey[i]=0; break; } } } }
static void icons_free(void){ for(int i=0;i<IC_NUM;i++){ free(ic_px[i]); ic_px[i]=NULL; } }
/* draw icon i centred in a box; bg = role colour behind it */
static void icon_draw(int i,int x,int y,int role_bg,int dim){
  if(i<0||i>=IC_NUM) return;
  if(!ic_px[i]||!can_rgb()){
    int tw=TextLength(RP,(STRPTR)ic_label[i],1);
    SetAPen(RP,(ULONG)tp[dim?P_FAINT:P_TEXT]); SetDrMd(RP,JAM1);
    Move(RP,OX+x+(IC_SZ-tw)/2,OY+y+(IC_SZ-RP->TxHeight)/2+RP->TxBaseline); Text(RP,(STRPTR)ic_label[i],1); return; }
  unsigned char out[IC_SZ*IC_SZ*3];
  ULONG bg=trgb[role_bg], tc=trgb[dim?P_FAINT:P_TEXT];
  if(role_bg==P_ACCENT) tc=0xFFFFFF;
  int br=bg>>16&255,bgc=bg>>8&255,bb=bg&255, tr=tc>>16&255,tg=tc>>8&255,tb=tc&255;
  for(int k=0;k<IC_SZ*IC_SZ;k++){
    const unsigned char *q=ic_px[i]+k*4; int a=q[3], r,g,b;
    if(ic_grey[i]){ int l=q[0]; r=tr*l/226; g=tg*l/226; b=tb*l/226; if(r>255) r=255; if(g>255) g=255; if(b>255) b=255; }
    else { r=q[0]; g=q[1]; b=q[2]; }
    out[k*3]=(unsigned char)((r*a+br*(255-a))/255); out[k*3+1]=(unsigned char)((g*a+bgc*(255-a))/255); out[k*3+2]=(unsigned char)((b*a+bb*(255-a))/255); }
  put_rgb(out,IC_SZ*3,x,y,IC_SZ,IC_SZ); }

/* ---- splash + About (Paint style) ---- */
static LONG g_splash_pen=-1;
static struct Window *splash_open(const char *status){
  int w=g_aga?480:512, h=g_aga?200:256+16;
  int iw=0,ih=0; unsigned char *px=load_png_file("PROGDIR:Icons/splash.png",&iw,&ih);
  if(px){ w=iw; h=ih+16; }
  if(w>scr->Width) w=scr->Width; if(h>scr->Height) h=scr->Height;
  struct Window *s=OpenWindowTags(NULL,WA_Left,(scr->Width-w)/2,WA_Top,(scr->Height-h)/2,WA_Width,w,WA_Height,h,
      WA_Borderless,TRUE,WA_CustomScreen,(ULONG)scr,WA_SmartRefresh,TRUE,WA_RMBTrap,TRUE,TAG_DONE);
  if(!s){ if(px) stbi_image_free(px); return NULL; }
  struct RastPort *srp=s->RPort, *save=RP; int sox=OX, soy=OY;
  RP=srp; OX=0; OY=0;
  /* 0.81: the MUI front end opens the splash before its main window, i.e. before detect_rtg(): with g_rtg still 0
     put_rgb() drew nothing on a 16/24/32-bit RTG Workbench, so the splash was a black box (beta report, 0.80). Ask the
     splash's own bitmap. */
  int ortg=g_rtg, oaga=g_aga;
  if(!g_rtg && srp->BitMap && ((P96Base && asb_p96GetBitMapAttr(srp->BitMap,ASB_P96BMA_ISP96)) ||
     (CyberGfxBase && asb_GetCyberMapAttr(srp->BitMap,ASB_CYBRMATTR_ISCYBERGFX)))){ g_rtg=1; g_aga=0; }
  asb_log("splash %dx%d image %s, %s",w,h,px?"ok":"missing",g_rtg?"RTG":g_aga?"AGA":g_clut?"8-bit pens":"no RGB path");
  LONG sbg=tp[P_SHADOW], stx=tp[P_TEXT];
  /* the status text needs a pen that shows on the dark band (System theme: the text pen can be black too) */
  if(g_splash_pen<0 && GfxBase->LibNode.lib_Version>=39) g_splash_pen=ObtainBestPenA(scr->ViewPort.ColorMap,0xE0E0E0E0,0xE0E0E0E0,0xE0E0E0E0,NULL);
  if(g_splash_pen>=0) stx=g_splash_pen;
  SetAPen(RP,(ULONG)sbg); RectFill(RP,0,0,w-1,h-1);
  if(px){ unsigned char *rgb=malloc((size_t)iw*ih*3);
    if(rgb){ for(int k=0;k<iw*ih;k++){ unsigned char *q=px+k*4; int a=q[3];
        rgb[k*3]=(unsigned char)((q[0]*a+0x14*(255-a))/255); rgb[k*3+1]=(unsigned char)((q[1]*a+0x14*(255-a))/255); rgb[k*3+2]=(unsigned char)((q[2]*a+0x22*(255-a))/255); }
      put_rgb(rgb,iw*3,0,0,iw<w?iw:w,ih<h-16?ih:h-16); free(rgb); }
    stbi_image_free(px); }
  else { SetAPen(RP,(ULONG)stx); SetDrMd(RP,JAM1); Move(RP,16,40); Text(RP,(STRPTR)"AmiSurf " ASB_VERSION,strlen("AmiSurf " ASB_VERSION)); }
  SetAPen(RP,(ULONG)sbg); RectFill(RP,0,h-16,w-1,h-1);
  SetAPen(RP,(ULONG)stx); SetDrMd(RP,JAM1); Move(RP,6,h-16+(16-RP->TxHeight)/2+RP->TxBaseline); Text(RP,(STRPTR)status,(ULONG)strlen(status));
  RP=save; OX=sox; OY=soy; g_rtg=ortg; g_aga=oaga;
  return s; }
static void splash_close(struct Window *s){
  if(s) CloseWindow(s);
  if(g_splash_pen>=0){ ReleasePen(scr->ViewPort.ColorMap,(ULONG)g_splash_pen); g_splash_pen=-1; } }
static void __attribute__((unused)) about_window(void){
  int w=480, h=0;
  { int th=g_font?g_font->tf_YSize:8; h=6*(th+6)+44; if(h<160) h=160; }
  struct Window *a=OpenWindowTags(NULL,WA_Left,(scr->Width-w)/2,WA_Top,(scr->Height-h)/2,WA_Width,w,WA_Height,h,
      WA_Title,(ULONG)"About AmiSurf " ASB_VERSION,WA_CustomScreen,(ULONG)scr,WA_DragBar,TRUE,WA_CloseGadget,TRUE,
      WA_Activate,TRUE,WA_RMBTrap,TRUE,WA_SmartRefresh,TRUE,WA_IDCMP,IDCMP_CLOSEWINDOW|IDCMP_MOUSEBUTTONS|IDCMP_VANILLAKEY|IDCMP_RAWKEY,TAG_DONE);
  if(!a) return;
  struct RastPort *save=RP; int sox=OX, soy=OY;
  RP=a->RPort; OX=a->BorderLeft; OY=a->BorderTop; if(g_font) SetFont(RP,g_font);
  int iw=a->Width-a->BorderLeft-a->BorderRight, ih=a->Height-a->BorderTop-a->BorderBottom;
  SetAPen(RP,(ULONG)tp[P_CHROME]); RectFill(RP,OX,OY,OX+iw-1,OY+ih-1);
  int lw=0,lh=0; unsigned char *px=load_png_file("PROGDIR:Icons/about.png",&lw,&lh);
  if(px && can_rgb() && lw<=64 && lh<=64){ unsigned char rgb[64*64*3]; ULONG c=trgb[P_CHROME];
    for(int k=0;k<lw*lh;k++){ unsigned char *q=px+k*4; int al=q[3];
      rgb[k*3]=(unsigned char)((q[0]*al+(int)(c>>16&255)*(255-al))/255); rgb[k*3+1]=(unsigned char)((q[1]*al+(int)(c>>8&255)*(255-al))/255); rgb[k*3+2]=(unsigned char)((q[2]*al+(int)(c&255)*(255-al))/255); }
    put_rgb(rgb,lw*3,12,(ih-lh)/2,lw,lh); }
  if(px) stbi_image_free(px);
  const char *l[6]={"AmiSurf " ASB_VERSION,"Pages rendered by Chrome or Firefox on the host","(asb-chromed), shown here over ASBC.","","Icons: Ken's Icons (Kenneth's DualPNG, as in AmiKit)","Toolbar: Lucide (ISC). stb_image, QOI: see ATTRIBUTIONS"};
  char hl[96]; snprintf(hl,sizeof hl,"%s %s:%d %s",g_legacy?"Engine":"AmiStudio Host",g_th,g_tp,g_fd>=0?"connected":"not connected"); l[3]=hl;
  SetDrMd(RP,JAM1);
  for(int i=0;i<6;i++){ SetAPen(RP,(ULONG)tp[i==0?P_TEXT:P_DIM]); Move(RP,OX+88,OY+14+i*(RP->TxHeight+6)+RP->TxBaseline); Text(RP,(STRPTR)l[i],(ULONG)strlen(l[i])); }
  RP=save; OX=sox; OY=soy;
  for(int done=0;!done;){
    WaitPort(a->UserPort); struct IntuiMessage *m;
    while((m=(struct IntuiMessage*)GetMsg(a->UserPort))){ ULONG c=m->Class; UWORD code=m->Code; ReplyMsg((struct Message*)m);
      /* SELECTDOWN, not UP: the menu click that opened this window releases inside it */
      if(c==IDCMP_CLOSEWINDOW||c==IDCMP_VANILLAKEY||(c==IDCMP_MOUSEBUTTONS&&code==SELECTDOWN)||(c==IDCMP_RAWKEY&&!(code&IECODE_UP_PREFIX))) done=1; } }
  CloseWindow(a); }
