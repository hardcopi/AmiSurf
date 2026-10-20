/* AmiSurf — 68k ASBC client. Chrome on the Linux host does the page. */
#define ASB_VERSION "0.87"
static const char asb_ver[] __attribute__((used)) = "$VER: AmiSurf 0.87 (19.10.2026)";
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/sghooks.h>
#include <graphics/gfx.h>
#include <utility/hooks.h>
#include <clib/alib_protos.h>
#include <devices/timer.h>
#include <proto/timer.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <workbench/startup.h>
#include <workbench/icon.h>
#include <proto/icon.h>
#include <devices/inputevent.h>
#include <devices/clipboard.h>
#include <devices/printer.h>
#include <libraries/gadtools.h>
#include <proto/gadtools.h>
#ifdef ASP_MUI
#include <libraries/mui.h>
#include <proto/muimaster.h>
#include <proto/keymap.h>
#include "muiinc/amiga_compiler.h"
#endif
#include <stdarg.h>
#include <proto/socket.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "../sys/rtg_amiga.h"
#include "../chrome/asbc.h"
#define QOI_IMPLEMENTATION
#define QOI_NO_STDIO
#include "../chrome/qoi.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_MAX_DIMENSIONS 4096
#include "../third_party/stb/stb_image.h"

/* 0.87: the stack. The icons carry no stack size, so from Workbench AmiSurf got the default stack (MUI 5 warns
   below 32 KB; in FS-UAE it overflowed at 4-12 KB), and this __stack was ignored: libnix only swaps to a stack of
   __stack bytes when its __stkinit startup module is linked, which the reference below does. the beta-reported Guru 8000 0004
   right after Settings... (0.85) is put down to that overflow. 256 KB is far above what we use (~11 KB). */
unsigned long __stack = 262144;
extern void __stkinit(void);
void *__asb_stkinit __attribute__((used)) = (void*)__stkinit;
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *P96Base, *CyberGfxBase, *SocketBase;
#ifdef ASP_MUI
struct Library *MUIMasterBase, *KeymapBase;
#endif

static struct Screen *scr;
static struct Window *win;
struct Library *GadToolsBase;
static struct RastPort *RP;
static int OX, OY, g_W, g_H, g_rtg;
static int g_tabh=22;
#define TABH g_tabh
static int g_btn=32, g_bstep=34, g_toolh=40, g_stath=16, g_full, g_zoom=100, g_findcur, g_findtot;
#define TOOLH g_toolh
static int g_bmh, g_bmbar=1;   /* g_bmbar: BOOKMARKSBAR pref; 0.87: GadTools bookmarks bar height (0: off; the MUI build has its own MUI bar) */
#define TOP (g_full?0:(TABH+TOOLH+g_bmh))
#define BTN g_btn        /* flat toolbar button, Paint style (TOOLBAR=LARGE: 40) */
#define BSTEP g_bstep
#define NLEFT 4          /* back fwd reload home */
#define NRIGHT 5         /* star bookmarks history find menu */
#define URLX (4+NLEFT*BSTEP+6)
#define LOCKW (IC_SZ+4)  /* padlock in front of the address */
static int g_fd=-1, g_loading, g_last_btn, g_find_on;
static char g_url[2400]="https://example.com/";
static char g_title[128]="AmiSurf";
static char g_token[ASBC_MAX_TOKEN+1];
static char g_home[2400]="";   /* 0.81: empty = the host's home page (Host settings); was the DuckDuckGo search page */
#define OLD_DEFAULT_HOME "https://html.duckduckgo.com/html/"
static char g_status[200];
static unsigned char *g_jbuf; static uint32_t g_jcap=ASBC_MAX_MSG;
static unsigned char *g_rgb, *g_scaled; static int g_pw, g_ph, g_rgb_str, g_cssw, g_cssh, g_sw, g_sh;
static char g_host[64]; static int g_port;   /* configured (Settings, HOST=/PORT= tooltypes, -host/-port): "" / 0 = automatic */
static char g_th[64]="127.0.0.1"; static int g_tp=ASBC_PORT, g_tsrc;   /* this connection attempt (target_pick) */
static int g_legacy, g_legacy_set; static char g_service[33]="browser";   /* HOSTMODE=LEGACY, SERVICE= */
static UBYTE g_sbuf[2400], g_sund[2400], g_swork[2400];
static UBYTE g_fbuf[200], g_fund[200];
static struct StringInfo g_si, g_fi;
static struct StringExtend g_sx;
static struct Hook g_hook;
static struct Gadget g_sg, g_fg;
static volatile int g_ctrl_h;
static int g_sg_ok, g_fg_ok, g_omni_sel, g_url_clicks, g_pending_paste, g_pending_copy;
static ULONG __attribute__((unused)) g_click_s, g_click_u;
#define MAXTAB 8
static char g_tabtitle[MAXTAB][64];
static int g_ntab=1, g_tab;
static LONG pen_bg=1, pen_fg=1, pen_face=2, pen_line=1, pens_got;
#include "asb_log.inc"
#include "asb_store.inc"   /* 0.86: PROGDIR:Data/, identity (ClientId/HostToken/Host), one-time ENV:/ENVARC: move */
static void send_cmd(const char *fmt,...);
#include "asb_audio.inc"
#include "asb_theme.inc"

#ifdef ASP_MUI
static int bot_h(void){ return 0; }
#else
static int bot_h(void){ return g_full?0:(g_find_on?g_stath*2+4:g_stath); }
#endif
static int content_h(void){ int h=g_H-TOP-bot_h(); return h>20?h:20; }
static void fillp(int x,int y,int w,int h,LONG pen);
static void ttext(int x,int y,const char *s);
static int url_editing(void);
static void send_hello(void);
#ifndef ASP_MUI
static void gt_bm_draw(void); static void gt_bm_click(int mx,int newtab);
#endif
static int g_bm_dirty;
static void pair_now(void); static void forget_token(int ask); static void host_menu(void); static void avsync_nudge(int d); static void avsync_menu(void);
static void send_nav(const char *u);
static void draw(void);
static void draw_chrome(void);
static int connect_host(void);
static void reconnect_soon(void);
static int g_replaced; /* host handed the session to another client: no auto-reconnect (avoids two clients stealing it back and forth) */
static void pop_draw(void);
static void ev_move(int mx,int my,UWORD qual); static void ev_button(UWORD code,int mx,int my); static void do_menu(int id,const char *arg);
static int g_popx, g_popy, g_popw, g_poph, g_pophov=-1, g_quit;
static volatile int g_popn;
#define POPMAX 30
enum { M_NONE, M_NEWTAB, M_REOPEN, M_ZOOMIN, M_ZOOMOUT, M_ZOOM0, M_FIND, M_BOOKMARKS, M_HISTORY, M_DOWNLOADS, M_PASSWORDS,
       M_HOSTSET, M_SAVEPNG, M_SAVEHTML, M_SAVEPDF, M_PRINT, M_SOURCE, M_SETTINGS, M_FULL, M_STATS, M_ABOUT, M_QUIT, M_TABLEFT, M_TABRIGHT,
       M_OPENTAB, M_OPEN, M_COPYLINK, M_SAVEIMG, M_COPYIMG, M_COPYSEL, M_BACK, M_FWD, M_RELOAD, M_BOOKMARK, M_SUGG,
       M_CLOSETAB, M_STOP, M_HOME, M_LOCATION, M_MUTE, M_PAIR, M_UNPAIR, M_HOSTMENU, M_AVMENU, M_AVLATER, M_AVEARLIER, M_AVRESET,
       M_BMBAR, M_BMMENU, M_BMOPEN, M_BMFOLDER, M_BMMORE, M_NUM };
static struct { char label[160]; int id; char arg[1200]; } g_pop[POPMAX];
static volatile int g_popkind, g_sugsel=-1, g_pop_dirty, g_sugg_req; /* kind 1 menu 2 context 3 suggestions */

static void draw_status(void);
static void tb_draw(int slot);
static void sync_url_gadget(void);
static void find_show(int on);
static void full_toggle(void);
static void settings_open(void);
static void do_copy(void);
static void do_paste(void);
static const char *url_text(void);
static void url_unfocus(void);
static void about_open(void);
static void url_focus(void);
static void find_paste(void);
#ifdef ASP_MUI
static int sugg_x(void);
static int sugg_w(void);
static Object *m_app, *m_win, *m_page, *m_url, *m_find;
#endif
static void scopy_u(char *d,size_t cap,const char *s){ size_t n=strlen(s); if(n>=cap) n=cap-1; memcpy(d,s,n); d[n]=0; }
static void set_status(const char *s){
  if(strcmp(s,g_status)) asb_log("status: %s",s);
  snprintf(g_status,sizeof g_status,"%s",s);
  draw_status();
}

static void send_cmd(const char *fmt,...){
  if(g_fd<0) return; char b[2600]; va_list ap; va_start(ap,fmt); vsnprintf(b,sizeof b,fmt,ap); va_end(ap);
  asbc_send(g_fd,ASBC_CMD,b,(uint32_t)strlen(b)); }
/* ---- speed stats (STATS tooltype / menu): client fps, KB/s, ms per frame; host encode ms from ASBC_STATS ---- */
struct Device *TimerBase; static struct timerequest g_tr; static int g_tmr;
static unsigned long long asb_us(void){
  if(!g_tmr){ if(OpenDevice((STRPTR)"timer.device",UNIT_ECLOCK,(struct IORequest*)&g_tr,0)) return 0; TimerBase=g_tr.tr_node.io_Device; g_tmr=1; }
  struct EClockVal e; ULONG f=ReadEClock(&e); return (((unsigned long long)e.ev_hi<<32)|e.ev_lo)*1000000ULL/f; }
static unsigned long g_sfr, g_sby, g_sus; static unsigned long long g_st0;   /* this second */
static char g_stxt[200]; static int g_hfps, g_henc; static unsigned long g_hkbs;
static void stats_tick(void){
  unsigned long long t=asb_us(); if(!g_st0){ g_st0=t; return; }
  if(t-g_st0<1000000ULL) return;
  unsigned long dt=(unsigned long)((t-g_st0)/1000); g_st0=t;
  snprintf(g_stxt,sizeof g_stxt,"%lu fps %lu KB/s %lu.%lums h%d.%dms",g_sfr*1000/dt,g_sby/dt,g_sfr?g_sus/g_sfr/1000:0,g_sfr?g_sus/g_sfr/100%10:0,g_henc/1000,g_henc/100%10);
  g_au_pks=g_au_pk*1000/dt; g_au_pk=0; g_au_unds=g_au_und; audio_log_tick(); audio_av_request();
  { int l=(int)strlen(g_stxt); audio_stats(g_stxt+l,(int)sizeof g_stxt-l); }
  g_sfr=g_sby=g_sus=0;
  if(g_stats) draw_status(); }
static void bm_add(void){ send_cmd("bookmark"); }
static void open_page(const char *name){ send_cmd("page %s",name); }
/* Home: the Amiga-side home page if one is set in Settings, else the host's (menu: Host settings) */
static void go_home(void){ if(g_home[0]) send_nav(g_home); else if(g_fd>=0){ asb_log("home: host home page"); send_cmd("home"); } }
static void prefs_load(void){
  FILE *f=fopen(PREF_FILE,"r");   /* 0.86: in the drawer (store_migrate() moved the ENVARC:/ENV: ones) */
  if(!f) return;
  char l[2400];
  while(fgets(l,sizeof l,f)){
    l[strcspn(l,"\r\n")]=0;
    if(!strncasecmp(l,"HOME=",5)) snprintf(g_home,sizeof g_home,"%s",strcmp(l+5,OLD_DEFAULT_HOME)?l+5:"");   /* the old default was saved by every Settings Save: treat it as "not set" */
    else if(!strncasecmp(l,"CHROMEHOST=",11) && l[11]) snprintf(g_host,sizeof g_host,"%s",l+11);
    else if(!strncasecmp(l,"PORT=",5)) g_port=atoi(l+5);
    else if(!strncasecmp(l,"TOKEN=",6)) snprintf(g_token,sizeof g_token,"%s",l+6);
    else if(!strncasecmp(l,"START=",6) && l[6]) snprintf(g_url,sizeof g_url,"%s",l+6);
    else if(!strncasecmp(l,"SCREENMODE=",11) && l[11]) theme_tooltype("SCREENMODE",l+11);
    else if(!strncasecmp(l,"BROWSER=",8)) theme_tooltype("BROWSER",l+8);
    else if(!strncasecmp(l,"URL=",4) && l[4]) snprintf(g_url,sizeof g_url,"%s",l+4);
    else { char *eq=strchr(l,'='); if(eq){ *eq=0; theme_tooltype(l,eq+1); } else theme_tooltype(l,NULL); }
  }
  fclose(f);
  if(g_port<0||g_port>65535) g_port=0;
}
static void tooltypes_load(int argc, char **argv){
  if(argc!=0||!argv) return;
  struct WBStartup *wb=(struct WBStartup*)argv;
  struct Library *IconBase=OpenLibrary((STRPTR)"icon.library",36);
  if(!IconBase||wb->sm_NumArgs<1){ if(IconBase) CloseLibrary(IconBase); return; }
  BPTR old=CurrentDir(wb->sm_ArgList[0].wa_Lock);
  struct DiskObject *d=GetDiskObject(wb->sm_ArgList[0].wa_Name);
  if(d){
    STRPTR v;
    if((v=FindToolType(d->do_ToolTypes,(STRPTR)"CHROMEHOST"))) snprintf(g_host,sizeof g_host,"%s",(char*)v);
    if((v=FindToolType(d->do_ToolTypes,(STRPTR)"PORT"))) g_port=atoi((char*)v);
    if((v=FindToolType(d->do_ToolTypes,(STRPTR)"TOKEN"))) snprintf(g_token,sizeof g_token,"%s",(char*)v);
    { static const char *k[]={"THEME","SCREENFONT","SCREENMODE","SPLASHHOLD","FRAMES","STATS","SOUND","LOG","BROWSER","AUTOHOST","HOSTCMD","AVSYNC","HOST","HOSTMODE","SERVICE","BOOKMARKSBAR",NULL};
      for(int i=0;k[i];i++) if((v=FindToolType(d->do_ToolTypes,(STRPTR)k[i]))) theme_tooltype(k[i],(char*)v);
      if(FindToolType(d->do_ToolTypes,(STRPTR)"MODEREQ")) theme_tooltype("MODEREQ",NULL); }
    if((v=FindToolType(d->do_ToolTypes,(STRPTR)"HOME"))) snprintf(g_home,sizeof g_home,"%s",strcmp((char*)v,OLD_DEFAULT_HOME)?(char*)v:"");
    if((v=FindToolType(d->do_ToolTypes,(STRPTR)"URL"))) snprintf(g_url,sizeof g_url,"%s",(char*)v);
    if((v=FindToolType(d->do_ToolTypes,(STRPTR)"START"))) snprintf(g_url,sizeof g_url,"%s",(char*)v);
    FreeDiskObject(d);
  }
  CurrentDir(old); CloseLibrary(IconBase);
}

static void be32(unsigned char *p, ULONG v){
  p[0]=(unsigned char)(v>>24); p[1]=(unsigned char)(v>>16);
  p[2]=(unsigned char)(v>>8); p[3]=(unsigned char)v;
}
static ULONG rd32(const unsigned char *p){
  return ((ULONG)p[0]<<24)|((ULONG)p[1]<<16)|((ULONG)p[2]<<8)|(ULONG)p[3];
}
static struct IOClipReq *clip_open(void){
  struct MsgPort *mp=CreateMsgPort();
  if(!mp) return NULL;
  struct IOClipReq *io=(struct IOClipReq*)CreateIORequest(mp,sizeof(struct IOClipReq));
  if(!io){ DeleteMsgPort(mp); return NULL; }
  if(OpenDevice((STRPTR)"clipboard.device",0,(struct IORequest*)io,0)){
    DeleteIORequest((struct IORequest*)io); DeleteMsgPort(mp); return NULL;
  }
  return io;
}
static void clip_close(struct IOClipReq *io){
  if(!io) return;
  struct MsgPort *mp=io->io_Message.mn_ReplyPort;
  CloseDevice((struct IORequest*)io);
  DeleteIORequest((struct IORequest*)io);
  if(mp) DeleteMsgPort(mp);
}
static void clip_write(const char *s, int n){
  if(!s||n<0) return;
  struct IOClipReq *io=clip_open(); if(!io) return;
  int pad=n&1;
  ULONG body=4+8+(ULONG)n+(ULONG)pad; /* FTXT + CHRS hdr + data */
  ULONG total=12+body;
  unsigned char *b=AllocVec(total,MEMF_PUBLIC|MEMF_CLEAR);
  if(!b){ clip_close(io); return; }
  memcpy(b,"FORM",4); be32(b+4,body); memcpy(b+8,"FTXT",4);
  memcpy(b+12,"CHRS",4); be32(b+16,(ULONG)n);
  if(n) memcpy(b+20,s,(size_t)n);
  io->io_ClipID=0; io->io_Offset=0;
  io->io_Command=CMD_WRITE; io->io_Data=b; io->io_Length=total;
  DoIO((struct IORequest*)io);
  io->io_Command=CMD_UPDATE; io->io_Data=NULL; io->io_Length=0;
  DoIO((struct IORequest*)io);
  FreeVec(b); clip_close(io);
}
static int clip_read(char *out, int cap){
  struct IOClipReq *io=clip_open(); if(!io||cap<2) return 0;
  unsigned char hdr[20];
  io->io_ClipID=0; io->io_Offset=0;
  io->io_Command=CMD_READ; io->io_Data=hdr; io->io_Length=20;
  DoIO((struct IORequest*)io);
  if(io->io_Actual<20 || memcmp(hdr,"FORM",4) || memcmp(hdr+8,"FTXT",4) || memcmp(hdr+12,"CHRS",4)){
    clip_close(io); return 0;
  }
  int n=(int)rd32(hdr+16); if(n<0) n=0; if(n>cap-1) n=cap-1;
  int got=0;
  if(n){
    io->io_Command=CMD_READ; io->io_Data=(APTR)out; io->io_Length=(ULONG)n;
    DoIO((struct IORequest*)io);
    got=(int)io->io_Actual; if(got<0) got=0; if(got>n) got=n;
  }
  /* drain rest */
  io->io_Command=CMD_READ; io->io_Data=hdr; io->io_Length=1;
  { int guard=0; do { DoIO((struct IORequest*)io); } while(io->io_Actual && ++guard<4096); }
  clip_close(io);
  out[got]=0; return got;
}
/* ---- clipboard keys inside our string fields (both front ends). Runs in the edit hook on
   input.device's task: only copy text into g_clip_out / set flags; the main loop does the I/O. ---- */
static char g_clip_out[2400]; static volatile int g_clip_out_n=-1;
/* returns 0 not ours, 1 handled, 2 select all (caller applies its own selection model) */
static int field_clipkey(struct SGWork *sgw,int allow_cmd,int which){
  struct InputEvent *ie=sgw->IEvent;
  UWORD q=ie?ie->ie_Qualifier:0, raw=ie?(UWORD)(ie->ie_Code&~IECODE_UP_PREFIX):0, ch=sgw->Code;
  int ctrl=!!(q&IEQUALIFIER_CONTROL), cmdk=!!(q&(IEQUALIFIER_LCOMMAND|IEQUALIFIER_RCOMMAND)), k=0;
  /* MUI 5's String class calls the edit hook without an InputEvent: a Ctrl-letter then only shows up as its
     control code (Ctrl-A = 1 ... Ctrl-Z = 26) being inserted */
  if(!ie && (sgw->EditOp==EO_INSERTCHAR||sgw->EditOp==EO_REPLACECHAR) && ch>=1 && ch<=26) ctrl=1;
  if(ctrl && !cmdk){ k=ch; if(k>=1 && k<=26) k+=96; else if(k>='A'&&k<='Z') k+=32; }
  else if(cmdk && allow_cmd){ k=raw==0x33?'c':raw==0x32?'x':raw==0x34?'v':raw==0x20?'a':0; }
  if(k=='c'||k=='x'){
    const char *src=(const char*)(sgw->PrevBuffer?sgw->PrevBuffer:sgw->StringInfo->Buffer); int n=0;
    while(src && src[n] && n<(int)sizeof g_clip_out-1){ g_clip_out[n]=src[n]; n++; }
    g_clip_out[n]=0; g_clip_out_n=n;
    if(k=='x'){ sgw->WorkBuffer[0]=0; sgw->NumChars=0; sgw->BufferPos=0; sgw->Actions=SGA_USE|SGA_REDISPLAY; }
    else sgw->Actions=(sgw->Actions&~SGA_USE)|SGA_REDISPLAY;
    return 1; }
  if(k=='v'){ g_pending_paste=which; sgw->Actions=(sgw->Actions&~SGA_USE)|SGA_REDISPLAY; return 1; }
  if(k=='a'){ sgw->Actions=(sgw->Actions&~SGA_USE)|SGA_REDISPLAY; return 2; }
  if(k=='h'){   /* Ctrl-H is Backspace. The hook runs after Intuition inserted the ^H, so start again from PrevBuffer. */
    if(!allow_cmd){ g_ctrl_h=which; sgw->Actions&=~SGA_USE; return 1; }   /* MUI: done in field_poll (MUI 5 ignores WorkBuffer edits) */
    const char *pb=(const char*)sgw->PrevBuffer; int p=sgw->StringInfo?sgw->StringInfo->BufferPos:0, n=0;
    if(!pb){ sgw->Actions&=~SGA_USE; return 1; }
    while(pb[n] && n<sgw->StringInfo->MaxChars-1){ sgw->WorkBuffer[n]=pb[n]; n++; } sgw->WorkBuffer[n]=0;
    if(p>n) p=n;
    if(p>0){ for(int i=p-1;i<n;i++) sgw->WorkBuffer[i]=sgw->WorkBuffer[i+1]; n--; p--; }
    sgw->NumChars=(WORD)n; sgw->BufferPos=(WORD)p;
    sgw->EditOp=EO_DELBACKWARD; sgw->Actions|=SGA_USE|SGA_REDISPLAY; return 0; }
  /* never insert control characters as text (they render as a box) */
  if((sgw->EditOp==EO_INSERTCHAR||sgw->EditOp==EO_REPLACECHAR) && (ch<32||ch==127||(ctrl&&!cmdk))){
    sgw->Actions=(sgw->Actions&~SGA_USE); return 1; }
  return 0; }
/* main loop: flush a pending copy from a field */
static void field_clip_flush(void){
  if(g_clip_out_n>=0){ int n=g_clip_out_n; g_clip_out_n=-1; clip_write(g_clip_out,n); set_status(n?"copied":"nothing to copy"); } }
/* paste text sanitized for a one-line field: newlines/tabs become spaces, other controls dropped */
static int clip_read_line(char *buf,int cap){
  int n=clip_read(buf,cap), o=0;
  for(int i=0;i<n;i++){ unsigned char c=(unsigned char)buf[i]; if(c=='\n'||c=='\r'||c=='\t') c=' '; if(c<32||c==127) continue; buf[o++]=(char)c; }
  while(o>0 && buf[o-1]==' ') o--;
  buf[o]=0; return o; }
static void page_clip(int mode){ unsigned char b=(unsigned char)mode; if(g_fd>=0) asbc_send(g_fd,ASBC_COPY,&b,1); }
#ifndef ASP_MUI
static void do_copy(void){
  if(url_editing()){
    clip_write((char*)g_sbuf,(int)strlen((char*)g_sbuf));
    set_status("copied address");
  } else page_clip(0);
}
static void gadget_set(const char *s){
  int n=s?(int)strlen(s):0; if(n>2046) n=2046;
  if(n) memcpy(g_sbuf,s,(size_t)n);
  g_sbuf[n]=0;
  g_si.BufferPos=(WORD)n;
  g_si.NumChars=(WORD)n;
  g_omni_sel=0;
  if(win && g_sg_ok) RefreshGList(&g_sg,win,NULL,1);
}
static void do_paste(void){
  char buf[4000]; int n=clip_read(buf,sizeof buf);
  if(n<=0){ set_status("clipboard empty"); return; }
  if(url_editing() && g_sg_ok){
    n=clip_read_line(buf,sizeof buf); if(n<=0) return;
    if(g_omni_sel) gadget_set(buf);
    else {
      int pos=g_si.BufferPos, cur=(int)strlen((char*)g_sbuf);
      if(pos<0||pos>cur) pos=cur;
      if(cur+n>=2046) n=2046-cur;
      if(n>0){
        memmove(g_sbuf+pos+n,g_sbuf+pos,(size_t)(cur-pos+1));
        memcpy(g_sbuf+pos,buf,(size_t)n);
        g_si.BufferPos=(WORD)(pos+n);
        g_si.NumChars=(WORD)(cur+n);
        RefreshGList(&g_sg,win,NULL,1);
      }
    }
  } else if(g_fd>=0) asbc_send(g_fd,ASBC_PASTE,buf,(uint32_t)n);
}

/* Called by Intuition via HookEntry (a0/a2/a1 → C args). Keep this off the input.device
   path for clipboard I/O: set g_pending_* and let the main loop DoIO. */
static ULONG url_edit_c(struct Hook *h, struct SGWork *sgw, ULONG *cmd){
  (void)h;
  if(!sgw||!cmd) return 0;
  if(*cmd==SGH_CLICK){
    struct InputEvent *ie=sgw->IEvent;
    int dbl=0;
    if(ie){
      dbl=DoubleClick(g_click_s,g_click_u,ie->ie_TimeStamp.tv_secs,ie->ie_TimeStamp.tv_micro);
      g_click_s=ie->ie_TimeStamp.tv_secs; g_click_u=ie->ie_TimeStamp.tv_micro;
    }
    (void)dbl;
    g_omni_sel=0;   /* 0.77: a click only places the cursor (it used to "select all" invisibly, so Backspace wiped the field) */
    g_url_clicks++;
    sgw->Actions|=SGA_USE|SGA_REDISPLAY;
    return 1;
  }
  if(*cmd!=SGH_KEY) return 0;
  {
    struct InputEvent *ie=sgw->IEvent;
    UWORD raw=ie?ie->ie_Code:0, ch=sgw->Code;
    if(raw&IECODE_UP_PREFIX) raw=(UWORD)(raw&~IECODE_UP_PREFIX);
    if(g_popkind==3 && g_popn && (raw==0x4D||raw==0x4C)){ /* cursor down/up walks the suggestions */
      int n=g_popn, k=g_sugsel; k=raw==0x4D?(k+1)%n:(k<=0?n-1:k-1); g_sugsel=k;
      const char *u=g_pop[k].arg; int l=(int)strlen(u); if(l>2046) l=2046;
      memcpy(sgw->WorkBuffer,u,(size_t)l); sgw->WorkBuffer[l]=0; sgw->NumChars=(WORD)l; sgw->BufferPos=(WORD)l; g_omni_sel=0;
      sgw->Actions=SGA_USE|SGA_REDISPLAY; g_pop_dirty=1; return 1; }
    if(raw==0x45) g_sugg_req=-1; else if(raw!=0x4F && raw!=0x4E && raw!=0x44 && raw!=0x43) g_sugg_req=1;
    { int r=field_clipkey(sgw,1,1);
      if(r==2){ g_omni_sel=2; sgw->BufferPos=sgw->NumChars; sgw->Actions|=SGA_REDISPLAY; return 1; }
      if(r==1){ if(sgw->Actions&SGA_USE) g_omni_sel=0; return 1; } }
    if(g_omni_sel){
      if(sgw->EditOp==EO_INSERTCHAR||sgw->EditOp==EO_REPLACECHAR){
        unsigned char c=(unsigned char)ch;
        if(c>=32){
          sgw->WorkBuffer[0]=c; sgw->WorkBuffer[1]=0;
          sgw->NumChars=1; sgw->BufferPos=1; g_omni_sel=0;
          sgw->Actions=SGA_USE|SGA_REDISPLAY;
          return 1;
        }
      }
      if(sgw->EditOp==EO_DELBACKWARD||sgw->EditOp==EO_DELFORWARD||sgw->EditOp==EO_CLEAR){
        if(g_omni_sel!=2){ g_omni_sel=0; return 1; }   /* only an explicit select-all (Ctrl-A) makes Backspace/Del clear everything */
        sgw->WorkBuffer[0]=0; sgw->NumChars=0; sgw->BufferPos=0; g_omni_sel=0;
        sgw->Actions=SGA_USE|SGA_REDISPLAY;
        return 1;
      }
      if(sgw->EditOp==EO_MOVECURSOR) g_omni_sel=0;
    }
  }
  return 1;
}

static UBYTE g_fwork[200];
static struct StringExtend g_fx;
static struct Hook g_fhook;
static ULONG find_edit_c(struct Hook *h, struct SGWork *sgw, ULONG *cmd){
  (void)h; if(!sgw||!cmd||*cmd!=SGH_KEY) return 0;
  if(field_clipkey(sgw,1,2)==2){ sgw->BufferPos=sgw->NumChars; }
  return 1; }
/* insert clipboard text at the cursor of an Intuition string gadget */
static void sg_insert(struct Gadget *g,struct StringInfo *si,const char *t,int n){
  int cur=(int)strlen((char*)si->Buffer), pos=si->BufferPos;
  if(pos<0||pos>cur) pos=cur;
  if(cur+n>si->MaxChars-1) n=si->MaxChars-1-cur;
  if(n<=0) return;
  memmove(si->Buffer+pos+n,si->Buffer+pos,(size_t)(cur-pos+1));
  memcpy(si->Buffer+pos,t,(size_t)n);
  si->BufferPos=(WORD)(pos+n); si->NumChars=(WORD)(cur+n);
  RefreshGList(g,win,NULL,1); }
static void find_paste(void){
  char buf[400]; int n=clip_read_line(buf,sizeof buf);
  if(n>0 && g_fg_ok && g_find_on) sg_insert(&g_fg,&g_fi,buf,n); }
static const char *url_text(void){ return (char*)g_sbuf; }
static void url_unfocus(void){
  if(g_sg_ok && (g_sg.Flags&GFLG_SELECTED)){ g_sg.Flags&=~GFLG_SELECTED; RefreshGList(&g_sg,win,NULL,1); g_omni_sel=0; g_url_clicks=0; } }
#endif
static void fillp(int x,int y,int w,int h,LONG pen){
  if(w<=0||h<=0||!RP) return;
  SetAPen(RP,(ULONG)pen);
  RectFill(RP, OX+x, OY+y, OX+x+w-1, OY+y+h-1);
}
static void ttext(int x,int y,const char *s){
  if(!s||!s[0]||!RP) return;
  SetAPen(RP,(ULONG)pen_fg); SetBPen(RP,(ULONG)pen_bg); SetDrMd(RP,JAM1);
  Move(RP,OX+x,OY+y+RP->TxBaseline); Text(RP,(STRPTR)s,(ULONG)strlen(s));
}
#ifndef ASP_MUI
static int g_pressed=-1;   /* toolbar button held down (drawn in accent) */
static int tb_icon(int slot){ /* slot 0..NLEFT-1 left, NLEFT.. right */
  static const int ic[NLEFT+NRIGHT]={IC_BACK,IC_FWD,IC_RELOAD,IC_HOME,IC_STAR,IC_BOOKMARKS,IC_HISTORY,IC_FIND,IC_MENU};
  int i=ic[slot]; if(i==IC_RELOAD && g_loading) i=IC_STOP; return i; }
static int tb_x(int slot){ return slot<NLEFT? 4+slot*BSTEP : g_W-NRIGHT*BSTEP-2+(slot-NLEFT)*BSTEP; }
static int tb_hit(int mx,int my){
  if(g_full) return -1;
  if(my<TABH+4||my>=TABH+4+BTN) return -1;
  for(int i=0;i<NLEFT+NRIGHT;i++){ int x=tb_x(i); if(mx>=x&&mx<x+BTN) return i; }
  return -1; }
static void tb_draw(int slot){
  if(g_full) return;
  int x=tb_x(slot), y=TABH+4;
  int active=(slot==g_pressed) || (slot==NLEFT+3 && g_find_on);
  int role=active?P_ACCENT:P_CHROME;
  fillp(x,y,BTN,BTN,tp[role]);
  icon_draw(tb_icon(slot),x+(BTN-IC_SZ)/2,y+(BTN-IC_SZ)/2,role,0);
}
#endif
static void detect_rtg(void){
  g_rtg=0;
  if(P96Base && win && win->RPort && win->RPort->BitMap &&
     asb_p96GetBitMapAttr(win->RPort->BitMap, ASB_P96BMA_ISP96)) g_rtg=1;
  if(!g_rtg && CyberGfxBase && win && win->RPort && win->RPort->BitMap &&
     asb_GetCyberMapAttr(win->RPort->BitMap, ASB_CYBRMATTR_ISCYBERGFX)) g_rtg=1;
  if(g_rtg) g_aga=0;
  clut_init();   /* 8-bit Workbench window: dithered onto shared Workbench pens */
}

static const unsigned char *scale_rgb(int dw,int dh){
  if(g_pw==dw && g_ph==dh) return g_rgb;
  if(!g_scaled || g_sw!=dw || g_sh!=dh){
    free(g_scaled);
    g_scaled=malloc((size_t)dw*(size_t)dh*3);
    g_sw=dw; g_sh=dh;
    if(!g_scaled) return g_rgb;
  }
  {
    ULONG xstep=((ULONG)g_pw<<16)/(ULONG)dw;
    int y;
    for(y=0;y<dh;y++){
      const unsigned char *srow=g_rgb+(y*g_ph/dh)*g_rgb_str;
      unsigned char *drow=g_scaled+y*dw*3;
      ULONG xacc=0; int x;
      for(x=0;x<dw;x++){
        const unsigned char *s=srow+(xacc>>16)*3;
        drow[0]=s[0]; drow[1]=s[1]; drow[2]=s[2];
        drow+=3; xacc+=xstep;
      }
    }
  }
  return g_scaled;
}
/* ---- 0.69 pre-converted frames: page kept in the screen's own pixel format, copied without decoding ---- */
static unsigned char *g_nat; static int g_natw, g_nath, g_natpf=-1, g_mypf=ASBC_PF_QOI; static ULONG g_p96fmt;
static int pf_of_p96(ULONG f){ switch(f){ case 2: return ASBC_PF_RGB24; case 3: return ASBC_PF_BGR24; case 4: return ASBC_PF_RGB565PC;
  case 5: return ASBC_PF_RGB555PC; case 6: return ASBC_PF_ARGB32; case 7: return ASBC_PF_ABGR32; case 8: return ASBC_PF_RGBA32;
  case 9: return ASBC_PF_BGRA32; case 10: return ASBC_PF_RGB565; case 11: return ASBC_PF_RGB555; case 12: return ASBC_PF_BGR565PC;
  case 13: return ASBC_PF_BGR555PC; default: return -1; } }
static void send_fmt(void){
  if(g_fd<0) return;
  int pf=ASBC_PF_QOI; g_p96fmt=ASB_RGBFB_R8G8B8;
  if(!g_frames_qoi){
    if(g_rtg && P96Base && win){ ULONG f=asb_p96GetBitMapAttr(win->RPort->BitMap,ASB_P96BMA_RGBFORMAT); int q=pf_of_p96(f);
      if(q>=0){ pf=q; g_p96fmt=f; } else pf=ASBC_PF_RGB24; }
    else if(g_rtg && CyberGfxBase) pf=ASBC_PF_RGB24;
    else if(g_aga||g_clut) pf=ASBC_PF_CLUT8; }
  g_mypf=pf;
  static unsigned char b[4+256*3]; int n=4;
  b[0]=(unsigned char)pf; b[1]=ASBC_FF_RLE|ASBC_FF_STATS|ASBC_FF_ACK|ASBC_FF_SCROLL; asbc_put_u16(b+2,0);
  if(pf==ASBC_PF_CLUT8){ int np=256;
    if(g_clut){ np=1<<(scr->RastPort.BitMap?(int)GetBitMapAttr(scr->RastPort.BitMap,BMA_DEPTH):8); if(np>256) np=256; }
    asbc_put_u16(b+2,(unsigned)np); for(int i=0;i<np;i++){ ULONG c=pen_rgb(i); b[4+i*3]=(unsigned char)(c>>16); b[5+i*3]=(unsigned char)(c>>8); b[6+i*3]=(unsigned char)c; } n=4+np*3; }
  asbc_send(g_fd,ASBC_FMT,b,(uint32_t)n); }
/* copy page area (x,y,w,h) of the native buffer to the window */
static void put_nat_raw(int x,int y,int w,int h);
/* page frames never paint over an open popup (menu, suggestions): blit only the parts around it, so the popup
   is not overdrawn and redrawn on every frame (that was the "strobing" over menus and the address list) */
static int g_nat_held, g_menustate;
/* an Intuition/MagicMenu menu strip is open on our window: do not render page frames over it (they are kept in
   g_nat and the whole page is repainted once the menu closes, see menu_state_poll) */
static int menu_busy(void){ return win && (win->Flags&WFLG_MENUSTATE); }
static struct Window *g_popwin;   /* 0.77: menus (main, right-click) live in their own borderless window */
/* 0.79: the main menu window (older MUI, GadTools) also holds page frames, like the menu strip: no page drawing
   next to or under it while it is open, the page is repainted once when it closes */
static int frames_held(void){ return menu_busy() || (g_popwin && g_popkind==1); }
static void put_nat(int x,int y,int w,int h){
  if(frames_held()){ g_nat_held=1; return; }
  if(!g_popn){ put_nat_raw(x,y,w,h); return; }
  int rx=g_popx-1, ry=g_popy-1-TOP, rw=g_popw+2, rh=g_poph+2;   /* popup in page coordinates */
  if(x>=rx+rw || x+w<=rx || y>=ry+rh || y+h<=ry){ put_nat_raw(x,y,w,h); return; }
  int y0=y>ry?y:ry, y1=y+h<ry+rh?y+h:ry+rh;
  if(y<ry) put_nat_raw(x,y,w,ry-y);                       /* above */
  if(y+h>ry+rh) put_nat_raw(x,ry+rh,w,y+h-(ry+rh));       /* below */
  if(x<rx) put_nat_raw(x,y0,rx-x,y1-y0);                  /* left */
  if(x+w>rx+rw) put_nat_raw(rx+rw,y0,x+w-(rx+rw),y1-y0);  /* right */
}
static void put_nat_raw(int x,int y,int w,int h){
  int vh=content_h();
#ifdef ASB_POPHB
  if(g_popwin){ static int c; if(!(c++&63)) asb_log("pophb put %d,%d %dx%d rp %p win rp %p layer %p",x,y,w,h,(void*)RP,(void*)win->RPort,RP?(void*)RP->Layer:NULL); }
#endif
  if(!g_nat || g_natpf<0) return;
  if(x<0){ w+=x; x=0; } if(y<0){ h+=y; y=0; }   /* 0.77: never index g_nat or the window outside the page */
  if(x+w>g_natw) w=g_natw-x; if(y+h>g_nath) h=g_nath-y; if(x+w>g_W) w=g_W-x; if(y+h>vh) h=vh-y;
  if(w<1||h<1||!RP||x>=g_natw||y>=g_nath) return;
  int bpp=asbc_pf_bpp(g_natpf); unsigned char *src=g_nat+((long)y*g_natw+x)*bpp;
  if(g_natpf==ASBC_PF_CLUT8){ WriteChunkyPixels(RP,(ULONG)(OX+x),(ULONG)(OY+TOP+y),(ULONG)(OX+x+w-1),(ULONG)(OY+TOP+y+h-1),src,(LONG)(g_natw)); return; }
  if(P96Base && g_rtg){ struct asb_p96_renderinfo ri; ri.Memory=(APTR)src; ri.BytesPerRow=(WORD)(g_natw*bpp); ri.pad=0; ri.RGBFormat=g_natpf==ASBC_PF_RGB24?ASB_RGBFB_R8G8B8:g_p96fmt;
    asb_p96WritePixelArray(&ri,0,0,RP,(UWORD)(OX+x),(UWORD)(OY+TOP+y),(UWORD)w,(UWORD)h); return; }
  if(CyberGfxBase && g_rtg && g_natpf==ASBC_PF_RGB24) asb_cgxWritePixelArray((APTR)src,0,0,(UWORD)(g_natw*3),RP,(UWORD)(OX+x),(UWORD)(OY+TOP+y),(UWORD)w,(UWORD)h,ASB_RECTFMT_RGB); }
static unsigned char *g_untile; static long g_untn;
static void apply_native(unsigned char *p,int n){
  int cssw=(int)asbc_get_u16(p), cssh=(int)asbc_get_u16(p+2), x=(int)asbc_get_u16(p+4), y=(int)asbc_get_u16(p+6);
  int tw=(int)asbc_get_u16(p+8), th=(int)asbc_get_u16(p+10), pf=p[13], comp=p[14];
  if(n<15 || pf!=g_mypf || pf==ASBC_PF_QOI || pf>=ASBC_PF_N || cssw<1 || cssh<1 || cssw>4096 || cssh>4096 || tw<1 || th<1 || x+tw>cssw || y+th>cssh){ snprintf(g_status,sizeof g_status,"bad raw frame pf=%d",pf); return; }
  int bpp=asbc_pf_bpp(pf);
  if(!g_nat || g_natw!=cssw || g_nath!=cssh || g_natpf!=pf){
    CRUMB("page buffer %dx%d pf %d (was %dx%d), view %dx%d",cssw,cssh,pf,g_natw,g_nath,g_W,content_h());
    free(g_nat); g_nat=calloc((size_t)cssw*cssh,(size_t)bpp); g_natw=cssw; g_nath=cssh; g_natpf=pf;
    if(!g_nat){ snprintf(g_status,sizeof g_status,"out of memory %dx%d",cssw,cssh); return; }
    free(g_rgb); g_rgb=NULL; g_pw=cssw; g_ph=cssh;
    if(RP && (cssw<g_W || cssh<content_h())) fillp(0,TOP,g_W,content_h(),tp[P_CANVAS]); }   /* smaller page: no stale pixels around it */
  long row=(long)tw*bpp; const unsigned char *src=p+15; int dn=n-15;
  if(comp==1){
    if((long)tw*th*bpp>g_untn){ free(g_untile); g_untn=(long)tw*th*bpp; g_untile=malloc((size_t)g_untn); if(!g_untile){ g_untn=0; return; } }
    if(x==0 && tw==cssw){ if(asbc_unrle(src,dn,g_nat+(long)y*row,tw*th,bpp)<0) return; }   /* full rows: unpack in place */
    else { if(asbc_unrle(src,dn,g_untile,tw*th,bpp)<0) return; src=g_untile; comp=0; } }
  else if(dn<(long)tw*th*bpp) return;
  if(comp==0){ long st=(long)cssw*bpp; unsigned char *d=g_nat+(long)y*st+(long)x*bpp;
    for(int j=0;j<th;j++,d+=st,src+=row) memcpy(d,src,(size_t)row); }
  put_nat(x,y,tw,th); }

static void blit_page(void);
/* host says: page rows [y..y+h) now hold the old rows shifted by dy. Move our copy, and on screen blit within the window. */
static void apply_scroll(unsigned char *p){
  int y=(int)asbc_get_u16(p+2), w=(int)asbc_get_u16(p+4), h=(int)asbc_get_u16(p+6), dy=asbc_get_i16(p+8);
  unsigned char *buf; int pw, ph, bpp;
  if(g_nat && g_natpf>=0){ buf=g_nat; pw=g_natw; ph=g_nath; bpp=asbc_pf_bpp(g_natpf); }
  else if(g_rgb){ buf=g_rgb; pw=g_pw; ph=g_ph; bpp=3; } else return;
  if(w!=pw || y<0 || h<1 || y+h>ph || y-dy<0 || y-dy+h>ph) return;
  long st=(long)pw*bpp; memmove(buf+(long)y*st,buf+(long)(y-dy)*st,(size_t)(h*st));
  int vh=content_h(), oneone=(pw==g_W && ph==vh);
  if(oneone && !g_popn && RP){ ClipBlit(RP,OX,OY+TOP+y-dy,RP,OX,OY+TOP+y,g_W,h,0xC0); }   /* screen to screen, no conversion */
  else if(g_nat && g_natpf>=0) put_nat(0,y,pw,h); else blit_page();
  g_sfr++; }
static void blit_page(void){
  if(g_nat && g_natpf>=0){ put_nat(0,0,g_natw,g_nath); return; }
  int vh=content_h();
  if(!g_rgb||g_pw<1||g_ph<1){
    ttext(12,TOP+16, g_fd<0? (g_legacy?"Start asb-chromed on the Linux host (HOSTMODE=LEGACY, port 9223)":"Not connected: AmiSurf needs the AmiStudio Host on a PC (status bar says why)"):"Waiting for first frame...");
    return;
  }
  int dw=g_W, dh=vh;
  if(dw<1||dh<1) return;
  if(!can_rgb()){ ttext(12,TOP+16,"Needs an RTG or AGA screen to show the page (SCREENMODE=WB on a non-RTG Workbench)."); return; }
  const unsigned char *pix=scale_rgb(dw,dh);
  int bpr=(pix==g_rgb)?g_rgb_str:dw*3;
  put_rgb(pix,bpr,0,TOP,dw,dh);
}

static void blit_dirty(int cx,int cy,int cw,int ch){
  if(g_nat && g_natpf>=0){ put_nat(cx,cy,cw,ch); return; }
  if(!g_rgb||g_pw<1||g_ph<1) return;
  int dw=g_W, dh=content_h();
  if(dw<1||dh<1) return;
  if(cx<=0&&cy<=0&&cw>=g_pw&&ch>=g_ph){ blit_page(); return; }
  int dx=cx*dw/g_pw, dy=cy*dh/g_ph;
  int dtw=(cx+cw)*dw/g_pw-dx, dth=(cy+ch)*dh/g_ph-dy;
  if(dtw<1) dtw=1; if(dth<1) dth=1;
  if(dx+dtw>dw) dtw=dw-dx; if(dy+dth>dh) dth=dh-dy;
  if(dtw<1||dth<1) return;
  unsigned char *tmp=malloc((size_t)dtw*(size_t)dth*3);
  if(!tmp){ blit_page(); return; }
  ULONG xstep=((ULONG)cw<<16)/(ULONG)dtw;
  int y;
  for(y=0;y<dth;y++){
    int sy=cy+y*ch/dth; if(sy>=g_ph) sy=g_ph-1;
    const unsigned char *srow=g_rgb+sy*g_rgb_str+cx*3;
    unsigned char *drow=tmp+y*dtw*3;
    ULONG xacc=0; int x;
    for(x=0;x<dtw;x++){
      const unsigned char *s=srow+(xacc>>16)*3;
      drow[0]=s[0]; drow[1]=s[1]; drow[2]=s[2];
      drow+=3; xacc+=xstep;
    }
  }
  put_rgb(tmp,dtw*3,dx,TOP+dy,dtw,dth);
  free(tmp);
}

#ifndef ASP_MUI
static int url_editing(void){ return g_sg_ok && (g_sg.Flags & GFLG_SELECTED); }

static void sync_url_gadget(void){
  if(!g_sg_ok || !win) return;
  if(url_editing()) return;
  snprintf((char*)g_sbuf,sizeof g_sbuf,"%s",g_url);
  g_si.BufferPos=(WORD)strlen((char*)g_sbuf);
  RefreshGList(&g_sg,win,NULL,1);
}

#endif
static int __attribute__((unused)) tab_w(void){ int n=g_ntab>0?g_ntab:1, tw=(g_W-40)/n; if(tw>200) tw=200; if(tw<56) tw=56; return tw; }
static int is_secure(void){ return !strncmp(g_url,"https://",8)||!strncmp(g_url,"asb://",6); }
#ifndef ASP_MUI
/* find bar layout (bottom): "Find:" [gadget] "3 of 17" [<] [>] [x] */
#define FB_BTN (g_stath+4)
static int fb_x(int k){ return g_W-(3-k)*(FB_BTN+4)-4; } /* k 0 prev 1 next 2 close */
static void draw_status(void){
  if(g_full) return;
  int by=g_H-bot_h();
  fillp(0,by,g_W,bot_h(),tp[P_CHROME]);
  fillp(0,by,g_W,1,tp[P_SEP]);
  SetDrMd(RP,JAM1);
  if(g_find_on){
    int fy=by+2+g_stath+2;
    SetAPen(RP,(ULONG)tp[P_TEXT]); Move(RP,OX+6,OY+fy+(g_stath-RP->TxHeight)/2+RP->TxBaseline); Text(RP,(STRPTR)"Find:",5);
    char c[40]; if(g_findtot) snprintf(c,sizeof c,"%d of %d",g_findcur,g_findtot); else snprintf(c,sizeof c,g_fbuf[0]?"no matches":"");
    int tw=TextLength(RP,(STRPTR)c,(ULONG)strlen(c));
    SetAPen(RP,(ULONG)tp[P_DIM]); Move(RP,OX+fb_x(0)-tw-10,OY+fy+(g_stath-RP->TxHeight)/2+RP->TxBaseline); Text(RP,(STRPTR)c,(ULONG)strlen(c));
    static const char *lb[3]={"<",">","x"};
    for(int k=0;k<3;k++){ int x=fb_x(k); fillp(x,fy-2,FB_BTN,g_stath+4,tp[P_FIELD]);
      SetAPen(RP,(ULONG)tp[P_TEXT]); Move(RP,OX+x+(FB_BTN-TextLength(RP,(STRPTR)lb[k],1))/2,OY+fy+(g_stath-RP->TxHeight)/2+RP->TxBaseline); Text(RP,(STRPTR)lb[k],1); }
    if(g_fg_ok) RefreshGList(&g_fg,win,NULL,1);
  }
  SetAPen(RP,(ULONG)tp[P_DIM]);
  const char *s=g_status[0]?g_status:g_title;
  Move(RP,OX+6,OY+by+1+(g_stath-1-RP->TxHeight)/2+RP->TxBaseline);
  { int n=(int)strlen(s); while(n>1&&TextLength(RP,(STRPTR)s,(ULONG)n)>g_W*3/5) n--; Text(RP,(STRPTR)s,(ULONG)n); }
  { char r[160]; char z[16]=""; if(g_zoom!=100) snprintf(z,sizeof z,"%d%%  ",g_zoom);
    if(g_stats) snprintf(r,sizeof r,"%s%s  %s %dx%d",z,g_stxt[0]?g_stxt:"stats...",g_rtg?"RTG":g_aga?"AGA":g_clut?"WB 8-bit":"WB",g_W,g_H); else
    snprintf(r,sizeof r,"%s%s%s %dx%d",z,g_fd>=0?"":"offline  ",g_rtg?"RTG":g_aga?"AGA 256":g_clut?"WB 8-bit":"WB",g_W,g_H);
    int n=(int)strlen(r); int tw=TextLength(RP,(STRPTR)r,(ULONG)n); SetAPen(RP,(ULONG)tp[P_FAINT]);
    Move(RP,OX+g_W-tw-6,OY+by+1+(g_stath-1-RP->TxHeight)/2+RP->TxBaseline); Text(RP,(STRPTR)r,(ULONG)n); }
}
static void draw_chrome(void){
  if(g_full) return;
  int i, x=4, n=g_ntab>0?g_ntab:1, tw=tab_w();
  /* tab strip (Paint: strip in TAB, active tab in CHROME, inactive text FAINT) */
  fillp(0,0,g_W,TABH,tp[P_TAB]);
  for(i=0;i<n && x+20<g_W;i++){
    int act=(i==g_tab);
    if(act) fillp(x,2,tw-4,TABH-2,tp[P_CHROME]);
    else fillp(x+tw-5,5,1,TABH-10,tp[P_SEP]);
    const char *s=(i<g_ntab && g_tabtitle[i][0])?g_tabtitle[i]:(act&&g_title[0]?g_title:"New Tab");
    int nl=(int)strlen(s); while(nl>1 && TextLength(RP,(STRPTR)s,(ULONG)nl)>tw-26) nl--;
    SetAPen(RP,(ULONG)tp[act?P_TEXT:P_FAINT]); SetDrMd(RP,JAM1);
    Move(RP,OX+x+8,OY+2+(TABH-2-RP->TxHeight)/2+RP->TxBaseline); Text(RP,(STRPTR)s,(ULONG)nl);
    Move(RP,OX+x+tw-16,OY+2+(TABH-2-RP->TxHeight)/2+RP->TxBaseline); Text(RP,(STRPTR)"x",1);
    x+=tw;
  }
  SetAPen(RP,(ULONG)tp[P_TEXT]); Move(RP,OX+x+6,OY+2+(TABH-2-RP->TxHeight)/2+RP->TxBaseline); Text(RP,(STRPTR)"+",1);
  /* toolbar */
  fillp(0,TABH,g_W,TOOLH,tp[P_CHROME]);
  fillp(0,TABH+TOOLH-1,g_W,1,tp[P_SEP]);
  for(i=0;i<NLEFT+NRIGHT;i++) tb_draw(i);
#ifndef ASP_MUI
  gt_bm_draw();
#endif
  if(g_sg_ok){ fillp(URLX-2,g_sg.TopEdge-OY-2,g_sg.Width+LOCKW+4,g_sg.Height+4,tp[P_FIELD]);
    icon_draw(is_secure()?IC_LOCK:IC_UNLOCK,URLX,g_sg.TopEdge-OY+(g_sg.Height-IC_SZ)/2,P_FIELD,!is_secure());
    RefreshGList(&g_sg,win,NULL,1); }
  draw_status();
}

static void draw(void){
  draw_chrome();
  blit_page();
  if(g_popn) pop_draw();
}

static void make_url_gadget(void){
  memset(&g_sg,0,sizeof g_sg);
  memset(&g_si,0,sizeof g_si);
  memset(&g_sx,0,sizeof g_sx);
  memset(&g_hook,0,sizeof g_hook);
  snprintf((char*)g_sbuf,sizeof g_sbuf,"%s",g_url);
  g_si.Buffer=g_sbuf;
  g_si.UndoBuffer=g_sund;
  g_si.MaxChars=2047;
  g_si.BufferPos=(WORD)strlen((char*)g_sbuf);
  g_si.NumChars=g_si.BufferPos;
  g_hook.h_Entry=(HOOKFUNC)HookEntry;
  g_hook.h_SubEntry=(HOOKFUNC)url_edit_c;
  g_sx.Pens[0]=(UBYTE)tp[P_TEXT]; g_sx.Pens[1]=(UBYTE)tp[P_FIELD];
  g_sx.ActivePens[0]=(UBYTE)tp[P_TEXT]; g_sx.ActivePens[1]=(UBYTE)tp[P_FIELD];
  g_sx.InitialModes=SGM_NOFILTER;
  g_sx.EditHook=&g_hook;
  g_sx.WorkBuffer=g_swork;
  g_si.Extension=&g_sx;
  g_sg.LeftEdge=(WORD)(OX+URLX+LOCKW);
  g_sg.Height=(WORD)(RP->TxHeight+4);
  g_sg.TopEdge=(WORD)(OY+TABH+4+(BTN-g_sg.Height)/2);
  g_sg.Width=(WORD)(g_W-URLX-LOCKW-NRIGHT*BSTEP-10); if(g_sg.Width<40) g_sg.Width=40;
  g_sg.Flags=GFLG_GADGHCOMP|GFLG_STRINGEXTEND;
  g_sg.Activation=GACT_RELVERIFY|GACT_IMMEDIATE|GACT_STRINGEXTEND;
  g_sg.GadgetType=GTYP_STRGADGET;
  g_sg.SpecialInfo=(APTR)&g_si;
  g_sg.GadgetID=1;
  AddGadget(win,&g_sg,~0);
  RefreshGList(&g_sg,win,NULL,1);
  g_sg_ok=1;
}

static void place_find_gadget(void){
  if(!g_fg_ok||!win) return;
  int fy=g_H-bot_h()+2+g_stath+2;
  g_fg.LeftEdge=(WORD)(OX+12+TextLength(RP,(STRPTR)"Find:",5));
  g_fg.TopEdge=(WORD)(OY+fy+(g_stath-RP->TxHeight-4)/2);
  g_fg.Width=(WORD)(fb_x(0)-140-(g_fg.LeftEdge-OX)); if(g_fg.Width<40) g_fg.Width=40;
  g_fg.Height=(WORD)(RP->TxHeight+4);
  RefreshGList(&g_fg,win,NULL,1);
}
static void find_show(int on){
  if(on && !g_fg_ok){
    memset(&g_fg,0,sizeof g_fg);
    memset(&g_fi,0,sizeof g_fi);
    g_fi.Buffer=g_fbuf; g_fi.UndoBuffer=g_fund; g_fi.MaxChars=180;
    memset(&g_fx,0,sizeof g_fx); memset(&g_fhook,0,sizeof g_fhook);
    g_fhook.h_Entry=(HOOKFUNC)HookEntry; g_fhook.h_SubEntry=(HOOKFUNC)find_edit_c;
    g_fx.Pens[0]=(UBYTE)tp[P_TEXT]; g_fx.Pens[1]=(UBYTE)tp[P_FIELD];
    g_fx.ActivePens[0]=(UBYTE)tp[P_TEXT]; g_fx.ActivePens[1]=(UBYTE)tp[P_FIELD];
    g_fx.EditHook=&g_fhook; g_fx.WorkBuffer=g_fwork; g_fi.Extension=&g_fx;
    g_fg.Flags=GFLG_GADGHCOMP|GFLG_STRINGEXTEND; g_fg.Activation=GACT_RELVERIFY|GACT_STRINGEXTEND;
    g_fg.GadgetType=GTYP_STRGADGET; g_fg.SpecialInfo=(APTR)&g_fi; g_fg.GadgetID=2;
    g_fg_ok=1;
  }
  if(!on && g_find_on) send_cmd("findclose");
  g_find_on=on; g_findcur=g_findtot=0;
  if(g_fg_ok){
    static int added;
    if(on){ if(!added){ AddGadget(win,&g_fg,~0); added=1; } place_find_gadget(); ActivateGadget(&g_fg,win,NULL); }
    else if(added){ g_fg.Flags&=~GFLG_SELECTED; RemoveGadget(win,&g_fg); added=0; }
  }
  send_hello();
  fillp(0,0,g_W,g_H,tp[P_CANVAS]);
  draw();
}
#endif
static void send_find(void){
#ifdef ASP_MUI
  const char *q=""; get(m_find,MUIA_String_Contents,&q); if(!q) q="";
#else
  const char *q=(char*)g_fbuf;
#endif
  if(g_fd>=0) asbc_send(g_fd,ASBC_FIND,q,(uint32_t)strlen(q));
}
static void apply_tabs(unsigned char *p,int n){
  if(n<2) return;
  int nt=p[0], act=p[1], o=2, i;
  if(nt>MAXTAB) nt=MAXTAB; if(nt<1) nt=1;
  g_ntab=nt; g_tab=act; if(g_tab<0||g_tab>=g_ntab) g_tab=0;
  for(i=0;i<nt && o+2<=n;i++){
    unsigned tl=asbc_get_u16(p+o); o+=2;
    int m=tl<(unsigned)sizeof g_tabtitle[0]-1?(int)tl:(int)sizeof g_tabtitle[0]-1;
    if(o+m>n) m=n-o; if(m<0) m=0;
    memcpy(g_tabtitle[i],p+o,(size_t)m); g_tabtitle[i][m]=0; o+=(int)tl;
  }
  draw_chrome();
}


static void send_hello(void){
  if(g_fd<0) return;
  unsigned char b[6]; asbc_put_u16(b,(unsigned)g_W); asbc_put_u16(b+2,(unsigned)content_h()); b[4]=(unsigned char)g_browser; /* older hosts read only w,h */
  b[5]=(unsigned char)audio_open();   /* audio formats we can play (0.74 hosts stream audio; older ones ignore it) */
  asbc_send(g_fd,ASBC_HELLO,b,b[5]?6:g_browser?5:4);
  if(b[5] && g_mute){ unsigned char m=0; asbc_send(g_fd,ASBC_AUDIOCTL,&m,1); }
  send_fmt();
}
static void send_nav(const char *u){
  if(g_fd<0||!u||!u[0]) return;
  uint32_t n=(uint32_t)strlen(u);
  if(u!=g_url) snprintf(g_url,sizeof g_url,"%s",u);
  g_loading=1; asbc_send(g_fd,ASBC_NAV,u,n);
}
static void send_mouse(int x,int y,int buttons,int ty){
  if(g_fd<0) return; y-=TOP; if(y<0||x<0) return;
  int dw=g_W, dh=content_h();
  if(dw<1) dw=1; if(dh<1) dh=1;
  if(x>=dw||y>=dh) return;
  int mx=x, my=y;
  if(g_cssw>0) mx=(int)((long)x*g_cssw/dw);
  if(g_cssh>0) my=(int)((long)y*g_cssh/dh);
  if(buttons) g_last_btn=buttons;
  int btn=buttons?buttons:(ty==ASBC_MUP?g_last_btn:0);
  unsigned char b[6]; asbc_put_u16(b,(unsigned)mx); asbc_put_u16(b+2,(unsigned)my); b[4]=(unsigned char)btn; b[5]=(unsigned char)ty;
  asbc_send(g_fd,ASBC_MOUSE,b,6);
}
static void send_key(int ty,int vk,int mods,const char *text){
  if(g_fd<0) return;
  unsigned char b[80]; int n=text?(int)strlen(text):0; if(n>64) n=64;
  b[0]=(unsigned char)ty; asbc_put_u16(b+1,(unsigned)vk); b[3]=(unsigned char)mods; asbc_put_u16(b+4,(unsigned)n);
  if(n) memcpy(b+6,text,(size_t)n);
  asbc_send(g_fd,ASBC_KEY,b,(uint32_t)(6+n));
}

static void apply_frame_in(unsigned char *p,int n);
static void apply_frame(unsigned char *p,int n){ unsigned long long t=asb_us(); apply_frame_in(p,n); g_sus+=(unsigned long)(asb_us()-t); g_sfr++; g_sby+=(unsigned long)n; }
static void apply_frame_in(unsigned char *p,int n){
  if(n<14) return;
  int cssw=(int)asbc_get_u16(p), cssh=(int)asbc_get_u16(p+2);
  int x=(int)asbc_get_u16(p+4), y=(int)asbc_get_u16(p+6);
  int tw=(int)asbc_get_u16(p+8), th=(int)asbc_get_u16(p+10);
  int fmt=p[12];
  if(fmt==3){ if(n>=15) apply_native(p,n); return; }
  if(g_nat){ free(g_nat); g_nat=NULL; g_natpf=-1; }
  unsigned char *tile=NULL; int iw=0,ih=0;
  if(fmt==2){
    qoi_desc d; memset(&d,0,sizeof d);
    tile=qoi_decode(p+14,n-14,&d,3);
    iw=(int)d.width; ih=(int)d.height;
  } else {
    int comp=0;
    tile=stbi_load_from_memory(p+14,n-14,&iw,&ih,&comp,3);
    if(!tile) tile=stbi_load_from_memory(p+4,n-4,&iw,&ih,&comp,3);
    if(tile){ x=0; y=0; tw=iw; th=ih; cssw=iw; cssh=ih; }
  }
  if(!tile||iw<1||ih<1){ free(tile); snprintf(g_status,sizeof g_status,"bad frame fmt=%d n=%d",fmt,n); return; }
  if(tw<1||th<1){ tw=iw; th=ih; }
  if(cssw<1) cssw=iw; if(cssh<1) cssh=ih;
  if(cssw>4096||cssh>4096){ free(tile); snprintf(g_status,sizeof g_status,"frame too big %dx%d",cssw,cssh); return; }
  if(!g_rgb || g_pw!=cssw || g_ph!=cssh){
    free(g_rgb);
    g_rgb=calloc((size_t)cssw*(size_t)cssh,3);
    g_pw=cssw; g_ph=cssh; g_rgb_str=cssw*3;
    if(!g_rgb){ snprintf(g_status,sizeof g_status,"out of memory %dx%d",cssw,cssh); free(tile); return; }
  }
  g_cssw=cssw; g_cssh=cssh;
  if(x<0) x=0; if(y<0) y=0;
  if(x+tw>cssw) tw=cssw-x; if(y+th>cssh) th=cssh-y;
  if(tw>iw) tw=iw; if(th>ih) th=ih;
  if(g_rgb && tw>0 && th>0){
    int row;
    for(row=0;row<th;row++)
      memcpy(g_rgb+((y+row)*g_pw+x)*3, tile+row*iw*3, (size_t)tw*3);
    blit_dirty(x,y,tw,th);
    if(g_popn) pop_draw();
  }
  free(tile);
}
static void apply_meta(unsigned char *p,int n){
  if(n<5) return;
  { int ld=!!(p[0]&4); if(ld!=g_loading){ g_loading=ld; tb_draw(2); } }
  int o=1; unsigned ul=asbc_get_u16(p+o); o+=2;
  if(o+(int)ul+2>n) return;
  int m=ul<(unsigned)sizeof g_url-1?(int)ul:(int)sizeof g_url-1;
  memcpy(g_url,p+o,(size_t)m); g_url[m]=0; o+=(int)ul;
  unsigned tl=asbc_get_u16(p+o); o+=2;
  if(o+(int)tl>n) tl=(unsigned)(n-o);
  m=tl<(unsigned)sizeof g_title-1?(int)tl:(int)sizeof g_title-1;
  memcpy(g_title,p+o,(size_t)m); g_title[m]=0;
#ifdef ASP_MUI
  if(!g_ownscr && m_win) set(m_win,MUIA_Window_Title,(ULONG)g_title);
#else
  if(!g_ownscr) SetWindowTitles(win,(STRPTR)g_title,(STRPTR)-1);
#endif
  sync_url_gadget();
  { static char last[2400]; if(strcmp(last,g_url)){ scopy_u(last,sizeof last,g_url); draw_chrome(); if(g_popn) pop_draw(); return; } }
  draw_status();
}


/* ---------- popup menus: main menu, right-click menu, address suggestions ---------- */
static int g_poprh=14;
static int pop_rowh(void){ return g_poprh>0?g_poprh:14; }
static void pop_win_close(void);
static void pop_add(const char *l,int id,const char *arg){
  if(g_popn>=POPMAX) return;
  snprintf(g_pop[g_popn].label,sizeof g_pop[0].label,"%s",l); g_pop[g_popn].id=id;
  snprintf(g_pop[g_popn].arg,sizeof g_pop[0].arg,"%s",arg?arg:""); g_popn++; }
static void pop_draw(void){
  if(!g_popn||!RP||g_popw<1||g_poph<1) return;
  struct RastPort *srp=RP; int sox=OX, soy=OY;
  if(g_popwin){ RP=g_popwin->RPort; if(srp->Font && RP->Font!=srp->Font) SetFont(RP,srp->Font); OX=-(g_popx-1); OY=-(g_popy-1); }
  int rh=pop_rowh(), i;
  /* frame as four lines: filling the whole box first made every redraw (hover, new suggestions) flash */
  fillp(g_popx-1,g_popy-1,g_popw+2,1,tp[P_BORDER]); fillp(g_popx-1,g_popy+g_poph,g_popw+2,1,tp[P_BORDER]);
  fillp(g_popx-1,g_popy,1,g_poph,tp[P_BORDER]); fillp(g_popx+g_popw,g_popy,1,g_poph,tp[P_BORDER]);
  for(i=0;i<g_popn;i++){
    int y=g_popy+i*rh, hov=(g_popkind==3?i==g_sugsel:i==g_pophov) && g_pop[i].id!=M_NONE;
    fillp(g_popx,y,g_popw,rh,hov?tp[P_ACCENT]:tp[P_DOCK]);
    if(g_pop[i].id==M_NONE && g_pop[i].label[0]=='-'){ fillp(g_popx+6,y+rh/2,g_popw-12,1,tp[P_SEP]); continue; }
    const char *t=g_pop[i].label; int n=(int)strlen(t); while(n>1 && TextLength(RP,(STRPTR)t,(ULONG)n)>g_popw-16) n--;
    SetAPen(RP,(ULONG)tp[g_pop[i].id==M_NONE?P_FAINT:P_TEXT]); SetDrMd(RP,JAM1);
    Move(RP,OX+g_popx+8,OY+y+(rh-RP->TxHeight)/2+RP->TxBaseline); Text(RP,(STRPTR)t,(ULONG)n); }
  RP=srp; OX=sox; OY=soy; }
static void pop_win_open(void);
static int g_popwin_failed;
/* place the popup inside x0..x1, y0..y1 (page coordinates) */
static void pop_fit(int x,int y,int w,int x0,int y0,int x1,int y1){
  int i, th=RP&&RP->TxHeight>0?RP->TxHeight:8, rh=th+(g_large?10:6), H=y1-y0, W=x1-x0;
  if(H>8 && g_popn*rh>H-2){ rh=(H-2)/g_popn; if(rh<th) rh=th; }   /* a long menu on a short window/screen gets tighter rows */
  g_poprh=rh;
  if(w<=0){ w=60; for(i=0;i<g_popn;i++){ int tw=TextLength(RP,(STRPTR)g_pop[i].label,(ULONG)strlen(g_pop[i].label))+24; if(tw>w) w=tw; } }
  if(w>W-6) w=W-6; if(w<8) w=8;
  g_poph=g_popn*rh;
  if(g_poph>H-1){ int fit=(H-1)/rh; if(fit<1) fit=1; if(g_popn>fit) g_popn=fit; g_poph=g_popn*rh; }   /* never taller than the area */
  if(x+w>g_W-2 && w<=g_W-4) x=g_W-2-w;              /* stay inside the page when it fits there */
  if(y+g_poph>g_H-2 && g_poph<=g_H-3) y=g_H-2-g_poph;
  if(x+w>x1) x=x1-w; if(x<x0) x=x0;
  if(y+g_poph>y1) y=y1-g_poph; if(y<y0) y=y0;
  g_popw=w; g_popx=x; g_popy=y; g_pophov=-1; }
static void pop_place(int x,int y,int w){
  if(g_popn<1){ g_popn=0; return; }
  if(g_W<16||g_H<8||!RP){ g_popn=0; g_popkind=0; return; }   /* window too small for any menu */
  if(g_popkind!=3 && win && scr && !g_popwin_failed){   /* 0.77: menus get their own window, so they may use the whole screen */
    int ox=win->LeftEdge+OX, oy=win->TopEdge+OY;
    pop_fit(x,y,w,2-ox,scr->BarHeight+2-oy,scr->Width-2-ox,scr->Height-2-oy); }
  else pop_fit(x,y,w,2,1,g_W-2,g_H-2);
  if(g_popkind!=3){ CRUMB("menu %d open: %d rows, %d,%d %dx%d in page %dx%d",g_popkind,g_popn,g_popx,g_popy,g_popw,g_poph,g_W,g_H);
    pop_win_open(); if(!g_popwin){ g_popwin_failed=1; pop_fit(x,y,w,2,1,g_W-2,g_H-2); g_popwin_failed=0; } } }
static void blit_screen_rect(int x,int y,int w,int h); /* restore what a popup covered */
static void pop_close(void){
  if(!g_popn) return;
  int x=g_popx-1,y=g_popy-1,w=g_popw+2,h=g_poph+2;
  if(g_popkind!=3) asb_log("menu %d closed",g_popkind);
  pop_win_close();
  g_popn=0; g_popkind=0; g_sugsel=-1; blit_screen_rect(x,y,w,h); }
static int pop_hit(int mx,int my){ /* row, -1 outside */
  if(!g_popn||mx<g_popx||mx>=g_popx+g_popw||my<g_popy||my>=g_popy+g_poph) return -1;
  return (my-g_popy)/pop_rowh(); }
static ULONG g_popgone_s, g_popgone_u;   /* when a menu window lost focus: the click that did it is swallowed */
static void pop_win_open(void){
  if(g_popwin || !win || !scr || !g_popn) return;
  int sx=win->LeftEdge+OX+g_popx-1, sy=win->TopEdge+OY+g_popy-1, w=g_popw+2, h=g_poph+2;
  if(sx<0||sy<0||sx+w>scr->Width||sy+h>scr->Height||w<3||h<3){ asb_log("menu window off screen (%d,%d %dx%d), drawn in the page",sx,sy,w,h); return; }
  g_popwin=OpenWindowTags(NULL, WA_Left,sx, WA_Top,sy, WA_Width,w, WA_Height,h, WA_CustomScreen,(ULONG)scr,
    WA_Borderless,TRUE, WA_Activate,TRUE, WA_RMBTrap,TRUE, WA_ReportMouse,TRUE, WA_SmartRefresh,TRUE, WA_NoCareRefresh,TRUE,
    WA_IDCMP,IDCMP_MOUSEBUTTONS|IDCMP_MOUSEMOVE|IDCMP_RAWKEY|IDCMP_INACTIVEWINDOW, TAG_DONE);
  if(!g_popwin) asb_log("menu window failed to open, drawn in the page");
  else CRUMB("menu window open at %d,%d %dx%d; page rp %s, layer %s",sx,sy,w,h,RP==win->RPort?"window":"other",RP&&RP->Layer?"yes":"none"); }
static void pop_win_close(void){
  if(!g_popwin) return;
  struct Window *w=g_popwin; g_popwin=NULL;
  struct IntuiMessage *m; Forbid(); while((m=(struct IntuiMessage*)GetMsg(w->UserPort))) ReplyMsg((struct Message*)m);
  CloseWindow(w); Permit();
  if(win) ActivateWindow(win); }
static ULONG pop_win_sig(void){ return g_popwin?1UL<<g_popwin->UserPort->mp_SigBit:0; }
/* 0.79 breadcrumbs: log when the menu window stops being the active or the front window, and what covers it */
static void pop_watch(void){
  static struct Window *lf; static int la=-1, lp=-1, ln;
  if(!g_popwin){ lf=NULL; la=-1; lp=-1; ln=0; return; }
  struct Layer *l=g_popwin->WLayer, *f; struct Window *fw=NULL; int n=0;
  Forbid(); for(f=l?l->front:NULL; f && n<64; f=f->front){ n++; fw=(struct Window*)f->Window; } Permit();   /* frontmost layer above us (read only) */
  int a=IntuitionBase->ActiveWindow==g_popwin, p=g_popwin->LeftEdge*4096+g_popwin->TopEdge;
  if(n && (fw!=lf || !ln)){ CRUMB("menu window covered by %d layer(s), front: %s \"%.40s\"",n,fw==win?"our window":fw?"window":"no window",fw&&fw->Title?(char*)fw->Title:"");
    lf=fw; ln=1; }
  else if(!n && ln){ CRUMB("menu window in front again"); lf=NULL; ln=0; }
  if(a!=la){ if(la>=0 || !a){ struct Window *aw=IntuitionBase->ActiveWindow;
      CRUMB("menu window %s, active: %s \"%.40s\"",a?"active":"not active",aw==win?"our window":aw==g_popwin?"menu":aw?"other":"none",aw&&aw->Title?(char*)aw->Title:""); } la=a; }
  if(lp>=0 && p!=lp) CRUMB("menu window moved to %d,%d",g_popwin->LeftEdge,g_popwin->TopEdge);
  lp=p;
#ifdef ASB_POPHB
  { static ULONG ls; ULONG s2,u2; CurrentTime(&s2,&u2); if(s2!=ls){ ls=s2; asb_log("pophb act %d front %d layers above %d win %p act %p",a,!n,n,(void*)g_popwin,(void*)IntuitionBase->ActiveWindow); } }
#endif
  }
static void pop_win_poll(void){
  struct IntuiMessage *m;
  pop_watch();
  while(g_popwin && (m=(struct IntuiMessage*)GetMsg(g_popwin->UserPort))){
    ULONG cl=m->Class; UWORD code=m->Code, qual=m->Qualifier; int mx=m->MouseX+g_popx-1, my=m->MouseY+g_popy-1;
    ReplyMsg((struct Message*)m);
    if(cl==IDCMP_MOUSEMOVE) ev_move(mx,my,qual);
    else if(cl==IDCMP_MOUSEBUTTONS){ if(pop_hit(mx,my)>=0 || !(code&IECODE_UP_PREFIX)) ev_button(code,mx,my); }
    else if(cl==IDCMP_INACTIVEWINDOW){ asb_log("menu window lost focus"); CurrentTime(&g_popgone_s,&g_popgone_u); pop_close(); }
    else if(cl==IDCMP_RAWKEY && !(code&IECODE_UP_PREFIX)){
      if(code==0x45) pop_close();   /* Esc */
      else if((code==0x4C||code==0x4D) && g_popn){ int k=g_pophov, n=g_popn, t;   /* cursor keys walk the menu */
        for(t=0;t<n;t++){ k=code==0x4D?(k+1+n)%n:(k<=0?n-1:k-1); if(g_pop[k].id!=M_NONE) break; }
        g_pophov=k; pop_draw(); }
      else if((code==0x44||code==0x43) && g_pophov>=0 && g_pophov<g_popn && g_pop[g_pophov].id!=M_NONE){
        int id=g_pop[g_pophov].id; char a[1200]; scopy_u(a,sizeof a,g_pop[g_pophov].arg); pop_close(); do_menu(id,a); } } } }
/* once per main-loop pass: repaint what an Intuition menu strip held back, log menu-strip use */
static void menu_state_poll(void){
  int b=menu_busy();
  if(b!=g_menustate){ g_menustate=b; asb_log(b?"menu strip open (page drawing paused)":"menu strip closed"); }
  if(!frames_held() && g_nat_held){ g_nat_held=0; if(g_nat && g_natpf>=0){ CRUMB("repaint after menu"); put_nat(0,0,g_natw,g_nath); } } }
static void blit_screen_rect(int x,int y,int w,int h){
  if(y<TOP-1 || y+h>g_H-bot_h()+1) draw_chrome();   /* (a popup's 1-pixel border on the edge does not redraw all the chrome) */
  if(g_nat && g_natpf>=0){   /* native frames: repaint just that rectangle, never the whole page */
    int y0=y<TOP?TOP:y, y1=y+h>g_H-bot_h()?g_H-bot_h():y+h, x0=x<0?0:x;
    if(y1>y0 && w>0) put_nat(x0,y0-TOP,x+w-x0,y1-y0);
    return; }
  if(y+h>TOP && y<g_H-bot_h() && g_rgb && g_pw>0 && g_ph>0){
    int dh=content_h(), y0=y<TOP?TOP:y, y1=y+h>g_H-bot_h()?g_H-bot_h():y+h;
    int cx=x*g_pw/g_W-1, cy=(y0-TOP)*g_ph/dh-1, cw=(w*g_pw+g_W-1)/g_W+3, ch=((y1-y0)*g_ph+dh-1)/dh+3;
    if(cx<0) cx=0; if(cy<0) cy=0; if(cx+cw>g_pw) cw=g_pw-cx; if(cy+ch>g_ph) ch=g_ph-cy;
    if(cw>0&&ch>0) blit_dirty(cx,cy,cw,ch); }
  else if(y+h>TOP) blit_page(); }

static void menu_main_items(void){
  pop_add("New tab              A-T",M_NEWTAB,NULL);
  pop_add("Reopen closed tab  A-S-T",M_REOPEN,NULL);
  pop_add("Move tab left      A-S-[",M_TABLEFT,NULL);
  pop_add("Move tab right     A-S-]",M_TABRIGHT,NULL);
  pop_add("-",M_NONE,NULL);
  { char z[48]; snprintf(z,sizeof z,"Zoom in   (now %d%%)  A-+",g_zoom); pop_add(z,M_ZOOMIN,NULL); }
  pop_add("Zoom out             A--",M_ZOOMOUT,NULL);
  pop_add("Actual size          A-0",M_ZOOM0,NULL);
  pop_add(g_full?"Leave full screen    F10":"Full screen          F10",M_FULL,NULL);
  pop_add(g_stats?"Hide speed stats":"Show speed stats",M_STATS,NULL);
  if(g_sound) pop_add(g_mute?"Sound on             A-M":"Mute sound           A-M",M_MUTE,NULL);
  pop_add("-",M_NONE,NULL);
  pop_add("Find in page         A-F",M_FIND,NULL);
  pop_add("Bookmarks            \xBB",M_BMMENU,NULL);   /* 0.87: submenu (bookmark, bar on/off, manage, the list) */
  pop_add("History              A-Y",M_HISTORY,NULL);
  pop_add("Downloads            A-J",M_DOWNLOADS,NULL);
  pop_add("Passwords",M_PASSWORDS,NULL);
  pop_add("-",M_NONE,NULL);
  pop_add("Save page as PNG     A-S",M_SAVEPNG,NULL);
  pop_add("Save page as HTML",M_SAVEHTML,NULL);
  pop_add("Save page as PDF     A-P",M_SAVEPDF,NULL);
  pop_add("Print (printer.device)",M_PRINT,NULL);
  pop_add("View source          A-U",M_SOURCE,NULL);
  pop_add("-",M_NONE,NULL);
  pop_add("Settings...          A-,",M_SETTINGS,NULL);
  pop_add("Host settings (home, search)",M_HOSTSET,NULL);
  pop_add("AmiStudio Host (pair) ...",M_HOSTMENU,NULL);
  if(g_sound){ char z[48]; snprintf(z,sizeof z,"Sound sync (now %+d ms) ...",av_out_ms()); pop_add(z,M_AVMENU,NULL); }
  pop_add("About                Help",M_ABOUT,NULL);
  pop_add("Quit                 A-Q",M_QUIT,NULL); }
static void menu_main(int x,int y){
  pop_close(); g_popkind=1; menu_main_items();
  pop_place(x,y,0); pop_draw(); }
static char g_ctxlink[1200], g_ctximg[1200], g_ctxsel[1200]; static int g_ctxx, g_ctxy;
static void menu_ctx(const unsigned char *p,int n){
  char b[3700]; int m=n<(int)sizeof b-1?n:(int)sizeof b-1; memcpy(b,p,(size_t)m); b[m]=0;
  char *l=b, *i=strchr(l,'\n'); if(i) *i++=0; else i="";
  char *sl=strchr(i,'\n'); if(sl) *sl++=0; else sl="";
  scopy_u(g_ctxlink,sizeof g_ctxlink,l); scopy_u(g_ctximg,sizeof g_ctximg,i); scopy_u(g_ctxsel,sizeof g_ctxsel,sl);
  pop_close(); g_popkind=2;
  if(g_ctxlink[0]){ pop_add("Open link in new tab",M_OPENTAB,g_ctxlink); pop_add("Open link",M_OPEN,g_ctxlink); pop_add("Copy link address",M_COPYLINK,g_ctxlink); pop_add("-",M_NONE,NULL); }
  if(g_ctximg[0]){ pop_add("Save image",M_SAVEIMG,g_ctximg); pop_add("Open image in new tab",M_OPENTAB,g_ctximg); pop_add("Copy image address",M_COPYIMG,g_ctximg); pop_add("-",M_NONE,NULL); }
  if(g_ctxsel[0]){ pop_add("Copy",M_COPYSEL,g_ctxsel); pop_add("-",M_NONE,NULL); }
  pop_add("Back",M_BACK,NULL); pop_add("Forward",M_FWD,NULL); pop_add("Reload",M_RELOAD,NULL);
  pop_add("-",M_NONE,NULL);
  pop_add("Bookmark this page",M_BOOKMARK,NULL); pop_add("Save page as PNG",M_SAVEPNG,NULL); pop_add("View source",M_SOURCE,NULL);
  pop_place(g_ctxx,g_ctxy,0); pop_draw(); }
static void sugg_apply(const unsigned char *p,int n){
  if(!url_editing()){ if(g_popkind==3) pop_close(); return; }
  int ox=0,oy=0,ow=0,oh=0;   /* old suggestion list: repainted below the new one only if the list got shorter */
  if(g_popkind!=3) pop_close(); else { ox=g_popx-1; oy=g_popy-1; ow=g_popw+2; oh=g_poph+2; g_popn=0; }
  g_popkind=3; g_popn=0; g_sugsel=-1;
  const unsigned char *e=p+n;
  while(p<e && g_popn<10){
    const unsigned char *nl=p; while(nl<e && *nl!='\n') nl++;
    char row[1400]; int m=(int)(nl-p); if(m>(int)sizeof row-1) m=(int)sizeof row-1; memcpy(row,p,(size_t)m); row[m]=0;
    char *t=strchr(row,'\t'); if(t) *t++=0;
    if(row[0]){ char lab[160]; snprintf(lab,sizeof lab,"%s  -  %s",t&&t[0]?t:row,row); pop_add(lab,M_SUGG,row); }
    p=nl+1; }
  if(!g_popn){ g_popkind=0; if(oh>0) blit_screen_rect(ox,oy,ow,oh); return; }
#ifdef ASP_MUI
  pop_place(sugg_x(),0,sugg_w());
#else
  pop_place(URLX,TABH+TOOLH,g_sg.Width+LOCKW);
#endif
  pop_draw();
  { int nb=g_popy-1+g_poph+2, obt=oy+oh;   /* uncover only what the old, longer list still hides */
    if(oh>0 && obt>nb) blit_screen_rect(ox,nb,ow,obt-nb);
    if(oh>0 && (ox<g_popx-1 || ox+ow>g_popx+g_popw+1)) blit_screen_rect(ox,oy,ow,oh>nb-oy?nb-oy:oh); } }

/* ---------- full screen, printing, settings ---------- */
#ifndef ASP_MUI
static void full_toggle(void){
  if(g_find_on) find_show(0);
  pop_close();
  g_full=!g_full;
  if(g_sg_ok){ if(g_full) RemoveGadget(win,&g_sg); else AddGadget(win,&g_sg,~0); }
  send_hello();
  fillp(0,0,g_W,g_H,tp[P_CANVAS]);
  if(!g_full) snprintf(g_status,sizeof g_status,"Full screen off");
  draw(); }
#endif
static void print_page(void){
  struct MsgPort *mp=CreateMsgPort(); if(!mp) return;
  struct IODRPReq *io=(struct IODRPReq*)CreateIORequest(mp,sizeof(struct IODRPReq));
  if(!io){ DeleteMsgPort(mp); return; }
  if(OpenDevice((STRPTR)"printer.device",0,(struct IORequest*)io,0)){ set_status("printer.device not available - use Save page as PDF"); DeleteIORequest((struct IORequest*)io); DeleteMsgPort(mp); return; }
  set_status("Printing...");
  io->io_Command=PRD_DUMPRPORT; io->io_RastPort=RP; io->io_ColorMap=scr->ViewPort.ColorMap;
  io->io_Modes=GetVPModeID(&scr->ViewPort); io->io_SrcX=(UWORD)OX; io->io_SrcY=(UWORD)(OY+TOP);
  io->io_SrcWidth=(UWORD)g_W; io->io_SrcHeight=(UWORD)content_h(); io->io_DestCols=0; io->io_DestRows=0;
  io->io_Special=SPECIAL_FULLCOLS|SPECIAL_ASPECT;
  LONG err=DoIO((struct IORequest*)io);
  char m[80]; if(err) snprintf(m,sizeof m,"Print failed (printer.device error %ld) - try Save page as PDF",(long)err); else snprintf(m,sizeof m,"Printed");
  CloseDevice((struct IORequest*)io); DeleteIORequest((struct IORequest*)io); DeleteMsgPort(mp);
  set_status(m); }
static void prefs_write(const char *path){
  FILE *f=fopen(path,"w"); if(!f) return;
  if(g_host[0]) fprintf(f,"CHROMEHOST=%s\n",g_host);
  if(g_port) fprintf(f,"PORT=%d\n",g_port);
  if(g_token[0]) fprintf(f,"TOKEN=%s\n",g_token);
  fprintf(f,"HOME=%s\nTHEME=%s\nSCREENFONT=%s\nTOOLBAR=%s\n",g_home,g_theme_pro?"PRO":"SYSTEM",g_font_topaz?"TOPAZ":"WB",g_large?"LARGE":"NORMAL");
  if(g_uifont[0]) fprintf(f,"UIFONT=%s/%d\n",g_uifont,g_uisize);
  if(g_browser) fprintf(f,"BROWSER=%s\n",g_browser==2?"FIREFOX":"CHROME");
  if(g_avsync_user) fprintf(f,"AVSYNC=%d\n",g_avsync_user);
  fprintf(f,"BOOKMARKSBAR=%s\n",g_bmbar?"ON":"OFF");   /* 0.84: the Sound sync menu's correction */
  if(g_wbmode) fprintf(f,"SCREENMODE=WB\n"); else if(g_modereq) fprintf(f,"MODEREQ\n");
  else if(g_modeid!=INVALID_ID) fprintf(f,"SCREENMODE=%08lx\n",(unsigned long)g_modeid);
  else fprintf(f,"SCREENMODE=FULL\n");
  fclose(f); }
/* store the Settings window's values (both front ends) */
static void settings_apply(const char *host,int port,const char *token,const char *home,const char *uifont,int theme,int fnt,int tool,int mode,int browser,int save){
    int obr=g_browser; g_browser=browser>=0&&browser<=2?browser:0;
    char oh[64]; int op=g_port; char ot[ASBC_MAX_TOKEN+1]; scopy_u(oh,sizeof oh,g_host); scopy_u(ot,sizeof ot,g_token);
    int olook=g_theme_pro*1000+g_font_topaz*100+g_large*10+(g_wbmode?0:g_modereq?2:1); char ouf[64]; snprintf(ouf,sizeof ouf,"%s/%d",g_uifont,g_uisize);
    scopy_u(g_host,sizeof g_host,host);
    g_port=port; if(g_port<0||g_port>65535) g_port=0;
    scopy_u(g_token,sizeof g_token,token);
    scopy_u(g_home,sizeof g_home,home);   /* empty = use the host's home page */
    if(uifont[0]) theme_tooltype("UIFONT",uifont); else g_uifont[0]=0;
    g_theme_pro=theme; g_font_topaz=fnt; g_large=tool; g_wbmode=mode==0; g_modereq=mode==2; if(mode!=1) g_modeid=INVALID_ID; /* 0 window, 1 full screen, 2 full screen + ask */
    if(save){ data_dir(); prefs_write(PREF_FILE); }   /* 0.86: Use = this run only (no ENV: copy) */
    int nlook=g_theme_pro*1000+g_font_topaz*100+g_large*10+(g_wbmode?0:g_modereq?2:1); char nuf[64]; snprintf(nuf,sizeof nuf,"%s/%d",g_uifont,g_uisize);
    set_status(olook!=nlook||strcmp(ouf,nuf)?"Settings saved - restart AmiSurf for the new look":"Settings saved");
    if(strcmp(oh,g_host)||op!=g_port||strcmp(ot,g_token)){ asb_log("settings: host %s:%d%s",g_host,g_port,strcmp(ot,g_token)?" (new token)":""); reconnect_soon(); }
    else if(obr!=g_browser && g_browser && g_fd>=0){ send_hello(); set_status(g_browser==2?"Switching the host to Firefox...":"Switching the host to Chrome..."); } }
#ifndef ASP_MUI
static void settings_open(void){
  if(!GadToolsBase) GadToolsBase=OpenLibrary((STRPTR)"gadtools.library",39);
  if(!GadToolsBase){ set_status("Settings need gadtools.library V39"); return; }
  APTR vi=GetVisualInfo(scr,TAG_END); if(!vi) return;
  struct Gadget *gl=NULL, *g=CreateContext(&gl), *gad[13];
  struct NewGadget ng; memset(&ng,0,sizeof ng);
  struct TextAttr *ta=scr->Font; int fh=ta?ta->ta_YSize:8; int rh=fh+8;
  int bl=scr->WBorLeft+4, bt=scr->WBorTop+fh+1+8, lw=fh*11, gw=fh*30;
  static STRPTR cy_theme[]={(STRPTR)"System (MUI/WB pens)",(STRPTR)"Pro (dark)",NULL};
  static STRPTR cy_font[]={(STRPTR)"Workbench font",(STRPTR)"Topaz 8",NULL};
  static STRPTR cy_tool[]={(STRPTR)"Normal",(STRPTR)"Large",NULL};
  static STRPTR cy_mode[]={(STRPTR)"Workbench window",(STRPTR)"Full screen (RTG, else AGA)",(STRPTR)"Full screen, ask mode",NULL};
  static STRPTR cy_br[]={(STRPTR)"Host default",(STRPTR)"Chrome",(STRPTR)"Firefox",NULL};
  char uif[64]=""; if(g_uifont[0]) snprintf(uif,sizeof uif,"%s/%d",g_uifont,g_uisize);
  int theme=g_theme_pro, fnt=g_font_topaz, tool=g_large, mode=g_wbmode?0:g_modereq?2:1, br=g_browser;
  ng.ng_VisualInfo=vi; ng.ng_TextAttr=ta; ng.ng_Flags=PLACETEXT_LEFT; ng.ng_Height=fh+6; ng.ng_Width=gw; ng.ng_LeftEdge=bl+lw;
  int r=0;
#define ROW(lbl,id) ng.ng_TopEdge=bt+(r++)*rh; ng.ng_GadgetText=(STRPTR)lbl; ng.ng_GadgetID=id
  ROW("Host",0); gad[0]=g=CreateGadget(STRING_KIND,g,&ng,GTST_String,(ULONG)g_host,GTST_MaxChars,63,TAG_END);
  ROW("Port",1); gad[1]=g=CreateGadget(INTEGER_KIND,g,&ng,GTIN_Number,g_port,GTIN_MaxChars,5,TAG_END);
  ROW("Token",2); gad[2]=g=CreateGadget(STRING_KIND,g,&ng,GTST_String,(ULONG)g_token,GTST_MaxChars,ASBC_MAX_TOKEN,TAG_END);
  ROW("Home page",3); gad[3]=g=CreateGadget(STRING_KIND,g,&ng,GTST_String,(ULONG)g_home,GTST_MaxChars,500,TAG_END);
  ROW("Browser",12); gad[12]=g=CreateGadget(CYCLE_KIND,g,&ng,GTCY_Labels,(ULONG)cy_br,GTCY_Active,br,TAG_END);
  ROW("Theme",4); gad[4]=g=CreateGadget(CYCLE_KIND,g,&ng,GTCY_Labels,(ULONG)cy_theme,GTCY_Active,theme,TAG_END);
  ROW("Screen font",5); gad[5]=g=CreateGadget(CYCLE_KIND,g,&ng,GTCY_Labels,(ULONG)cy_font,GTCY_Active,fnt,TAG_END);
  ROW("UI font",6); gad[6]=g=CreateGadget(STRING_KIND,g,&ng,GTST_String,(ULONG)uif,GTST_MaxChars,60,TAG_END);
  ROW("Toolbar",7); gad[7]=g=CreateGadget(CYCLE_KIND,g,&ng,GTCY_Labels,(ULONG)cy_tool,GTCY_Active,tool,TAG_END);
  ROW("Screen",8); gad[8]=g=CreateGadget(CYCLE_KIND,g,&ng,GTCY_Labels,(ULONG)cy_mode,GTCY_Active,mode,TAG_END);
#undef ROW
  int bw=(lw+gw-16)/3; ng.ng_Flags=PLACETEXT_IN; ng.ng_Width=bw; ng.ng_TopEdge=bt+r*rh+8;
  ng.ng_LeftEdge=bl; ng.ng_GadgetText=(STRPTR)"Save"; ng.ng_GadgetID=9; gad[9]=g=CreateGadget(BUTTON_KIND,g,&ng,TAG_END);
  ng.ng_LeftEdge=bl+bw+8; ng.ng_GadgetText=(STRPTR)"Use"; ng.ng_GadgetID=10; gad[10]=g=CreateGadget(BUTTON_KIND,g,&ng,TAG_END);
  ng.ng_LeftEdge=bl+2*(bw+8); ng.ng_GadgetText=(STRPTR)"Cancel"; ng.ng_GadgetID=11; gad[11]=g=CreateGadget(BUTTON_KIND,g,&ng,TAG_END);
  if(!g){ FreeGadgets(gl); FreeVisualInfo(vi); set_status("Settings: could not create gadgets"); return; }
  int ww=bl+lw+gw+12, wh=ng.ng_TopEdge+ng.ng_Height+8+scr->WBorBottom;
  struct Window *w=OpenWindowTags(NULL,WA_Left,(scr->Width-ww)/2,WA_Top,(scr->Height-wh)/2,WA_Width,ww,WA_Height,wh,
      WA_Title,(ULONG)"AmiSurf Settings",WA_CustomScreen,(ULONG)scr,WA_Gadgets,(ULONG)gl,
      WA_DragBar,TRUE,WA_DepthGadget,TRUE,WA_CloseGadget,TRUE,WA_Activate,TRUE,WA_SimpleRefresh,TRUE,WA_RMBTrap,TRUE,
      WA_IDCMP,STRINGIDCMP|CYCLEIDCMP|BUTTONIDCMP|IDCMP_CLOSEWINDOW|IDCMP_REFRESHWINDOW|IDCMP_VANILLAKEY,TAG_END);
  if(!w){ FreeGadgets(gl); FreeVisualInfo(vi); return; }
  GT_RefreshWindow(w,NULL);
  int done=0, act=0;
  while(!done){
    WaitPort(w->UserPort); struct IntuiMessage *m;
    while((m=GT_GetIMsg(w->UserPort))){
      ULONG cl=m->Class; UWORD code=m->Code; struct Gadget *gg=(struct Gadget*)m->IAddress; GT_ReplyIMsg(m);
      if(cl==IDCMP_CLOSEWINDOW || (cl==IDCMP_VANILLAKEY && code==27)){ done=1; act=0; }
      else if(cl==IDCMP_VANILLAKEY && code==13){ done=1; act=10; }
      else if(cl==IDCMP_REFRESHWINDOW){ GT_BeginRefresh(w); GT_EndRefresh(w,TRUE); }
      else if(cl==IDCMP_GADGETUP){ int id=gg->GadgetID;
        if(id==12) br=code; else if(id==4) theme=code; else if(id==5) fnt=code; else if(id==7) tool=code; else if(id==8) mode=code;
        else if(id>=9 && id<=11){ done=1; act=id; } } } }
  if(act==9||act==10) settings_apply((char*)((struct StringInfo*)gad[0]->SpecialInfo)->Buffer,(int)((struct StringInfo*)gad[1]->SpecialInfo)->LongInt,
      (char*)((struct StringInfo*)gad[2]->SpecialInfo)->Buffer,(char*)((struct StringInfo*)gad[3]->SpecialInfo)->Buffer,
      (char*)((struct StringInfo*)gad[6]->SpecialInfo)->Buffer,theme,fnt,tool,mode,br,act==9);
  CloseWindow(w); FreeGadgets(gl); FreeVisualInfo(vi);
  draw(); }
#endif

#include "asb_bmarks.inc"
#ifndef ASP_MUI
#include "asb_bmbar_gt.inc"
#endif
static void do_menu(int id,const char *arg){
  unsigned char ix;
  switch(id){
  case M_NEWTAB: if(g_fd>=0) asbc_send(g_fd,ASBC_TABNEW,NULL,0); break;
  case M_REOPEN: send_cmd("reopen"); break;
  case M_TABLEFT: if(g_tab>0) send_cmd("tabmove %d %d",g_tab,g_tab-1); break;
  case M_TABRIGHT: if(g_tab+1<g_ntab) send_cmd("tabmove %d %d",g_tab,g_tab+1); break;
  case M_ZOOMIN: send_cmd("zoom +"); break;
  case M_ZOOMOUT: send_cmd("zoom -"); break;
  case M_ZOOM0: send_cmd("zoom 0"); break;
  case M_FULL: full_toggle(); break;
  case M_STATS: g_stats=!g_stats; draw_status(); break;
  case M_MUTE: if(g_sound){ audio_mute(!g_mute); set_status(g_mute?"sound muted":"sound on"); } break;
  case M_FIND: find_show(1); break;
  case M_BOOKMARKS: open_page("bookmarks"); break;
  case M_HISTORY: open_page("history"); break;
  case M_DOWNLOADS: open_page("downloads"); break;
  case M_PASSWORDS: open_page("passwords"); break;
  case M_HOSTSET: open_page("settings"); break;
  case M_PAIR: pair_now(); break;
  case M_UNPAIR: forget_token(1); break;
  case M_HOSTMENU: host_menu(); break;
  case M_AVMENU: avsync_menu(); break;
  case M_AVLATER: avsync_nudge(50); break;
  case M_AVEARLIER: avsync_nudge(-50); break;
  case M_AVRESET: avsync_nudge(0); break;
  case M_SAVEPNG: send_cmd("savepng"); break;
  case M_SAVEHTML: send_cmd("savehtml"); break;
  case M_SAVEPDF: send_cmd("savepdf"); break;
  case M_PRINT: print_page(); break;
  case M_SOURCE: send_cmd("source"); break;
  case M_SETTINGS: settings_open(); break;
  case M_ABOUT: about_open(); break;
  case M_QUIT: g_quit=1; break;
  case M_OPENTAB: send_cmd("newtab %s",arg); break;
  case M_OPEN: case M_SUGG: send_nav(arg); break;
  case M_COPYLINK: case M_COPYIMG: case M_COPYSEL: clip_write(arg,(int)strlen(arg)); set_status("Copied"); break;
  case M_SAVEIMG: send_cmd("saveimg %s",arg); break;
  case M_BACK: if(g_fd>=0) asbc_send(g_fd,ASBC_BACK,NULL,0); break;
  case M_FWD: if(g_fd>=0) asbc_send(g_fd,ASBC_FWD,NULL,0); break;
  case M_RELOAD: if(g_fd>=0) asbc_send(g_fd,ASBC_RELOAD,NULL,0); break;
  case M_BOOKMARK: bm_add(); break;
  case M_BMBAR: bmbar_toggle(); break;
  case M_BMMENU: menu_bm(g_popx,g_popy); break;   /* (the main menu was just closed at g_popx,g_popy) */
  case M_BMOPEN: bm_open(atoi(arg),qual_shift()); break;
  case M_BMFOLDER: menu_bm_folder(atoi(arg),g_popx,g_popy); break;
  case M_BMMORE: menu_bm_rest(g_popx,g_popy); break;
  case M_CLOSETAB: ix=(unsigned char)g_tab; if(g_fd>=0) asbc_send(g_fd,ASBC_TABCLS,&ix,1); break;
  case M_STOP: if(g_fd>=0) asbc_send(g_fd,ASBC_STOP,NULL,0); break;
  case M_HOME: go_home(); break;
  case M_LOCATION: url_focus(); break;
  default: (void)ix; break; } }

/* Amiga-key shortcuts (also reachable with Ctrl where Intuition keeps Amiga-key combos) */
static int shortcut(int c,int shift){
  unsigned char ix;
  switch(c){
  case 'q': g_quit=1; return 1;
  case '[': if(shift) do_menu(M_TABLEFT,NULL); else if(g_fd>=0) asbc_send(g_fd,ASBC_BACK,NULL,0); return 1;
  case ']': if(shift) do_menu(M_TABRIGHT,NULL); else if(g_fd>=0) asbc_send(g_fd,ASBC_FWD,NULL,0); return 1;
  case 'r': if(g_fd>=0) asbc_send(g_fd,ASBC_RELOAD,NULL,0); return 1;
  case '.': if(g_fd>=0) asbc_send(g_fd,ASBC_STOP,NULL,0); return 1;
  case 'f': find_show(1); return 1;
  case 'g': if(!g_find_on) find_show(1); else send_cmd(shift?"findprev":"findnext"); return 1;
  case 't': if(shift) send_cmd("reopen"); else if(g_fd>=0) asbc_send(g_fd,ASBC_TABNEW,NULL,0); return 1;
  case 'w': ix=(unsigned char)g_tab; if(g_fd>=0) asbc_send(g_fd,ASBC_TABCLS,&ix,1); return 1;
  case 'l': url_focus(); return 1;
  case '=': case '+': send_cmd("zoom +"); return 1;
  case '-': send_cmd("zoom -"); return 1;
  case '0': send_cmd("zoom 0"); return 1;
  case 'u': send_cmd("source"); return 1;
  case 's': send_cmd("savepng"); return 1;
  case 'p': send_cmd("savepdf"); return 1;
  case 'j': open_page("downloads"); return 1;
  case 'o': open_page("bookmarks"); return 1;
  case 'y': open_page("history"); return 1;
  case 'd': bm_add(); return 1;
  case 'b': bmbar_toggle(); return 1;
  case 'h': go_home(); return 1;
  case ',': settings_open(); return 1;
  case 'c': do_copy(); return 1;
  case 'v': do_paste(); return 1;
  case 'm': if(!g_sound) return 1; audio_mute(!g_mute); set_status(g_mute?"sound muted":"sound on"); return 1;
  }
  if(c>='1'&&c<='8'){ ix=(unsigned char)(c-'1'); if(ix<g_ntab && g_fd>=0) asbc_send(g_fd,ASBC_TABSEL,&ix,1); return 1; }
  return 0; }
static int raw2char(int raw){
  static const struct { unsigned char r; char c; } t[]={{0x10,'q'},{0x11,'w'},{0x13,'r'},{0x14,'t'},{0x15,'y'},{0x16,'u'},{0x18,'o'},{0x19,'p'},
    {0x1A,'['},{0x1B,']'},{0x35,'b'},{0x21,'s'},{0x22,'d'},{0x23,'f'},{0x24,'g'},{0x25,'h'},{0x26,'j'},{0x28,'l'},{0x33,'c'},{0x34,'v'},{0x37,'m'},
    {0x38,','},{0x39,'.'},{0x0A,'0'},{0x0B,'-'},{0x0C,'='},{0x01,'1'},{0x02,'2'},{0x03,'3'},{0x04,'4'},{0x05,'5'},{0x06,'6'},{0x07,'7'},{0x08,'8'},
    {0x5E,'+'},{0x4A,'-'},{0x0F,'0'}};
  for(unsigned i=0;i<sizeof t/sizeof t[0];i++) if(t[i].r==raw) return t[i].c;
  return 0; }

static int sock_pending(int fd){
  fd_set rf; FD_ZERO(&rf); FD_SET(fd,&rf);
  struct timeval tv; tv.tv_sec=0; tv.tv_usec=0;
  ULONG z=0;
  return WaitSelect(fd+1,&rf,NULL,NULL,&tv,&z)>0 && FD_ISSET(fd,&rf);
}
static void pump(void){
  if(g_fd<0) return;
  int last_n=0, saw=0;
  ULONG t0s=0,t0u=0; CurrentTime(&t0s,&t0u);
  ULONG uimask=(win&&win->UserPort?1UL<<win->UserPort->mp_SigBit:0)|pop_win_sig();
  for(;;){
    int op=0; WHERE("reading a message from the host"); int n=asbc_recv(g_fd,&op,g_jbuf,g_jcap);
    { static char w[48]; snprintf(w,sizeof w,"handling host message %d (%d bytes)",op,n); WHERE(w); }
    if(n<0){ asb_log("host connection lost"); CloseSocket(g_fd); g_fd=-1; g_replaced=!strncmp(g_status,"replaced",8); snprintf(g_status,sizeof g_status,g_replaced?"replaced by another client - click to reconnect":"host disconnected"); draw(); return; }
    saw=1;
    if(op==ASBC_AUDIO){ audio_packet(g_jbuf,n); if(!sock_pending(g_fd)) break; continue; }
    audio_service();   /* keep Paula/AHI fed while frames are decoded */
    if(op==ASBC_FRAME){ apply_frame(g_jbuf,n); last_n=n; asbc_send(g_fd,ASBC_ACK,NULL,0);
#ifdef ASB_AVPROBE   /* test build: av.html's white box (page 300,250) appeared */
      if(g_nat && g_natw>300 && g_nath>250){ int b=asbc_pf_bpp(g_natpf); const unsigned char *q=g_nat+((long)250*g_natw+300)*b; static int was;
        int w=b>=3 && q[0]+q[1]+q[2]>700; if(w && !was) avprobe_mark(0); was=w; }
#endif
    }
    else if(op==ASBC_SCROLL){ if(n>=10) apply_scroll(g_jbuf); asbc_send(g_fd,ASBC_ACK,NULL,0); }
    else if(op==ASBC_META) apply_meta(g_jbuf,n);
    else if(op==ASBC_STATUS){
      int m=n<(int)sizeof g_status-1?n:(int)sizeof g_status-1;
      memcpy(g_status,g_jbuf,(size_t)m); g_status[m]=0;
      set_status(g_status);
    } else if(op==ASBC_CTX) menu_ctx(g_jbuf,n);
    else if(op==ASBC_SUGG) sugg_apply(g_jbuf,n);
    else if(op==ASBC_STATS){ if(n>=10){ g_hfps=(int)asbc_get_u16(g_jbuf); g_hkbs=((unsigned long)asbc_get_u16(g_jbuf+2)|((unsigned long)asbc_get_u16(g_jbuf+4)<<16))/1024; g_henc=(int)(asbc_get_u16(g_jbuf+6)|((unsigned long)asbc_get_u16(g_jbuf+8)<<16)); }
      if(n>=14){ g_vlat=(int)asbc_get_u16(g_jbuf+10); g_hlat=(int)asbc_get_u16(g_jbuf+12); } }
    else if(op==ASBC_ZOOM){ if(n>=2){ g_zoom=(int)asbc_get_u16(g_jbuf); draw_status(); } }
    else if(op==ASBC_FINDRES){ if(n>=4){ g_findcur=(int)asbc_get_u16(g_jbuf); g_findtot=(int)asbc_get_u16(g_jbuf+2); draw_status(); } }
    else if(op==ASBC_CLIP){
      clip_write((char*)g_jbuf,n);
      snprintf(g_status,sizeof g_status,"copied %d chars",n);
      set_status(g_status);
    } else if(op==ASBC_TABS) apply_tabs(g_jbuf,n);
    else if(op==ASBC_BMARKS) bm_apply(g_jbuf,n);
    if(!sock_pending(g_fd)) break;
    /* 0.77: never let a stream of frames starve the mouse and keyboard (on a slow Amiga a playing video kept
       this loop busy for many seconds, so a menu click seemed to hang the machine) */
    if(SetSignal(0,0)&uimask) break;
    { ULONG s2,u2; CurrentTime(&s2,&u2); if((long)(s2-t0s)*1000+((long)u2-(long)t0u)/1000>100) break; }
  }
  (void)last_n; (void)saw;
}

/* AmiKit "Rabbit Hole": when the host on this PC is not running, start PROGDIR:Host/run-host.sh on the Linux side
   through Amiberry's C:host-run (it maps an existing Amiga path to the host path), then wait up to ~15 s */
static int is_loopback(void){ return !strcmp(g_th,"127.0.0.1")||!strcasecmp(g_th,"localhost"); }
static LONG run_cmd(const char *cmd,int async){
  BPTR in=Open((STRPTR)"NIL:",MODE_OLDFILE), out=Open((STRPTR)"NIL:",MODE_NEWFILE);
  LONG rc=SystemTags((STRPTR)cmd,SYS_Input,(ULONG)in,SYS_Output,(ULONG)out,SYS_Asynch,(ULONG)(async?TRUE:FALSE),TAG_DONE);
  if(!async || rc==-1){ if(in) Close(in); if(out) Close(out); }
  return rc; }
static int g_disc_due;   /* (asb_ash.inc) */
static int g_tcpfail;   /* the last connect_host() failed at TCP level (nothing listening), not in the handshake */
/* 0.83: the AmiKit edition bundles the AmiStudio Host: Host/run-host.sh starts amistudio-hostd on 127.0.0.1:9220,
   which starts the engine when we connect. HOSTMODE=LEGACY: run-host.sh --legacy starts plain asb-chromed on 9223. */
static int autohost(void){
  if(g_fd>=0 || !is_loopback() || g_autohost==0) return -1;
  if(!g_legacy && !g_tcpfail) return -1;   /* a Host answered (not paired, no browser service ...): its requester says why */
  char cmd[900];
  if(g_hostcmd[0]) snprintf(cmd,sizeof cmd,"%s",g_hostcmd);
  else {
    BPTR l=Lock((STRPTR)"C:host-run",ACCESS_READ); if(!l) return -1; UnLock(l);   /* no Rabbit Hole: nothing to do */
    char path[512]; l=Lock((STRPTR)"PROGDIR:Host/run-host.sh",ACCESS_READ);
    if(!l){ if(g_legacy) set_status("Browser engine not installed: no Host/run-host.sh in the AmiSurf drawer"); return -1; }
    if(!NameFromLock(l,(STRPTR)path,sizeof path)){ UnLock(l); return -1; } UnLock(l);
    /* the drawer must be on a host directory drive (not inside a hardfile) for Linux to see the script */
    { char hp[600]; snprintf(hp,sizeof hp,"C:host-path \"%s\"",path); BPTR t=Lock((STRPTR)"C:host-path",ACCESS_READ);
      if(t){ UnLock(t); if(run_cmd(hp,0)!=0){ set_status("Host/ is not on a drive Linux can see - start Host/run-host.sh on Linux (README-AMIKIT)"); return -1; } } }
    if(g_legacy) snprintf(cmd,sizeof cmd,"C:host-run sh \"%s\" --background --legacy --port %d --idle 30",path,g_tp);
    else snprintf(cmd,sizeof cmd,"C:host-run sh \"%s\" --background --port %d --idle 30",path,g_tp); }   /* --background: Host 0.1.2's run-host.sh needs it to detach */
  /* the AmiStudio launcher (0.1.4+) may be starting a Host through Rabbit Hole right now: look once more before
     starting ours (run-host.sh also refuses to start a second Host on the port) */
  set_status("Looking for the AmiStudio Host ..."); Delay(50);
  if(connect_host()==0){ g_disc_due=0; asb_log("autohost: a Host answers on %s:%d now, not starting one",g_th,g_tp); return 0; }
  if(!g_legacy && !g_tcpfail){ asb_log("autohost: a Host answers on %s:%d (but refused), not starting one",g_th,g_tp); return -1; }
  asb_log("autohost: %s",cmd);
  if(run_cmd(cmd,1)==-1){ set_status("Could not run the host command"); return -1; }
  for(int i=0;i<30;i++){
    char s[80]; snprintf(s,sizeof s,g_legacy?"Starting browser engine... %d":"Starting the AmiStudio Host... %d",(30-i+1)/2); set_status(s);
    Delay(25); if(connect_host()==0){ g_disc_due=0; return 0; }
    if(!g_legacy && !g_tcpfail) break; }   /* the Host answered but said no: stop waiting, the requester explains */
  if(g_fd<0 && (g_legacy||g_tcpfail)) set_status(g_legacy?"Browser engine did not start - see Host/data/host.log in the AmiSurf drawer"
                                         :"The AmiStudio Host did not start - see Host/data/host.log in the AmiSurf drawer");
  return -1; }
static void log_startup(void){
  UWORD af=SysBase->AttnFlags;
  const char *cpu=(af&0x80)?"68060":(af&AFF_68040)?"68040":(af&AFF_68030)?"68030":(af&AFF_68020)?"68020":"68000";
  int depth=scr&&scr->RastPort.BitMap?(int)GetBitMapAttr(scr->RastPort.BitMap,BMA_DEPTH):0;
  asb_log("AmiSurf %s (%s build) CPU %s%s, Kickstart %d.%d, %s",ASB_VERSION,
#ifdef ASP_MUI
#ifdef ASP_020
    "MUI 020",
#else
    "MUI 040",
#endif
#else
    "GadTools",
#endif
    cpu,(af&(AFF_68881|AFF_FPU40))?"+FPU":"",SysBase->LibNode.lib_Version,SysBase->SoftVer,g_logpath);
  STKLOG("startup");
#ifdef ASP_MUI
  asb_log("MUI muimaster.library %d.%d",MUIMasterBase?MUIMasterBase->lib_Version:0,MUIMasterBase?MUIMasterBase->lib_Revision:0);
#endif
  asb_log("screen %dx%d depth %d, %s, %s; window %dx%d page %dx%d; host %s:%d",scr?scr->Width:0,scr?scr->Height:0,depth,
    g_rtg?"RTG":g_aga?"AGA":g_clut?"Workbench palette":"Workbench",g_ownscr?"own screen":"Workbench window",win?win->Width:0,win?win->Height:0,g_W,g_H,g_th,g_tp); }
/* ---- connecting (0.81: bounded) ----
   Before 0.81 connect() and the ASBC handshake blocked without a limit: a host/port that does not answer (or answers
   but is not asb-chromed, e.g. the page server on PORT+1 or a web server) froze the whole client, and the offline
   loop retried that every 2 s - after a host change in Settings the window never came back (beta report, 0.80).
   Now: non-blocking connect with a 3 s limit, handshake with a 4 s limit, the name lookup is cached per host, and
   failed attempts back off (2, 4, 8, 16, 30 s). Reconnects after a Settings change happen from the main loop. */
#include <libraries/bsdsocket.h>
#define CONNECT_MS 3000
#define HANDSHAKE_MS 4000
static ULONG g_rc_next; static int g_rc_fail, g_rc_now=1;
static char g_rs_host[64]; static ULONG g_rs_addr;
static ULONG ms_now(void){ ULONG s,u; CurrentTime(&s,&u); return s*1000UL+u/1000UL; }   /* wraps; compare by difference */
static int wait_sock(int s,int wr,long ms){
  fd_set f; FD_ZERO(&f); FD_SET(s,&f);
  struct timeval tv; tv.tv_sec=ms/1000; tv.tv_usec=(ms%1000)*1000; ULONG z=0;
  return WaitSelect(s+1,wr?NULL:&f,wr?&f:NULL,NULL,&tv,&z)>0; }
/* the client handshake (ASH to the AmiStudio Host, or plain ASBC in HOSTMODE=LEGACY) with a time limit:
   0 ok, -1 failed, -2 token rejected, -3 timeout, -(10+status) ASH status (asb_ash.inc) */
static int hs_build(unsigned char *b); static int hs_need(const unsigned char *b,int have); static int hs_parse(const unsigned char *b,int n);
static int handshake_to(int s,long ms){
  unsigned char b[400];
  ULONG t0=ms_now(); int len=hs_build(b), off=0;
  /* a few bytes on a fresh connection: send() returns at once. No WaitSelect() on a write set: 0.80 never used one,
     and in UAE's emulation the window's later WaitSelect() calls sometimes stopped waking after it */
  while(off<len){ long k=send(s,b+off,len-off,0); if(k<=0) return -1; off+=(int)k; }
  off=0;
  while(off<hs_need(b,off)){ long left=ms-(long)(ms_now()-t0); if(left<=0) return -3;
    if(!wait_sock(s,0,left>250?250:left)) continue;
    long k=recv(s,b+off,hs_need(b,off)-off,0); if(k<=0) return -1; off+=(int)k; }
  return hs_parse(b,off); }
static int resolve_host(struct sockaddr_in *a){
  memset(a,0,sizeof *a); a->sin_family=AF_INET; a->sin_port=htons((UWORD)g_tp);
  unsigned A,B,C,D; char x;
  if(sscanf(g_th,"%u.%u.%u.%u%c",&A,&B,&C,&D,&x)==4 && A<256&&B<256&&C<256&&D<256){ a->sin_addr.s_addr=htonl((A<<24)|(B<<16)|(C<<8)|D); return 0; }
  if(!g_th[0]){ snprintf(g_status,sizeof g_status,"no host set (Settings)"); return -1; }
  if(g_rs_host[0] && !strcmp(g_rs_host,g_th)){ a->sin_addr.s_addr=g_rs_addr; return 0; }
  WHERE("looking up the host name");
  struct hostent *he=gethostbyname((STRPTR)g_th);
  if(!he||!he->h_addr_list[0]){ snprintf(g_status,sizeof g_status,"unknown host %.60s",g_th); return -1; }
  memcpy(&a->sin_addr,he->h_addr_list[0],4); scopy_u(g_rs_host,sizeof g_rs_host,g_th); g_rs_addr=a->sin_addr.s_addr; return 0; }
static void connected_ok(int s){
  g_fd=s; snprintf(g_status,sizeof g_status,"connected"); asb_log("connected to %s:%d",g_th,g_tp);
  g_rc_fail=0; g_av_sent=0;
  send_hello();
  send_cmd("bookmarks");   /* 0.87: the list for the bookmarks bar (an older engine ignores it) */
  if(g_url[0]) send_nav(g_url); }
/* connect() bounded by a timer signal in bsdsocket's break mask (the call returns EINTR). No non-blocking connect:
   in UAE's bsdsocket emulation a socket connected that way stopped waking WaitSelect() (the window froze until
   some other input came - seen in FS-UAE while testing 0.81). */
/* After a timed-out connect, UAE's emulation keeps working on it in the background (Linux gives up after ~130 s), and
   the next bsdsocket call from this task waits for that: do not call connect() again before then. */
#define COOL_MS 135000
static ULONG g_cool_until; static int g_cool;
static long cool_left(void){ if(!g_cool) return 0; long l=(long)(g_cool_until-ms_now()); if(l<=0){ g_cool=0; return 0; } return l; }
static int connect_timed(int s,struct sockaddr_in *a,long ms,int *timed){
  if(cool_left()>0){ *timed=1; return -1; }
  struct MsgPort *mp=CreateMsgPort(); struct timerequest *tr=mp?(struct timerequest*)CreateIORequest(mp,sizeof(struct timerequest)):NULL;
  int topen=tr && !OpenDevice((STRPTR)TIMERNAME,UNIT_VBLANK,(struct IORequest*)tr,0);
  ULONG bit=mp?1UL<<mp->mp_SigBit:0;
  if(topen){ struct TagItem t[2]; t[0].ti_Tag=SBTM_SETVAL(SBTC_BREAKMASK); t[0].ti_Data=SIGBREAKF_CTRL_C|bit; t[1].ti_Tag=TAG_END; t[1].ti_Data=0; SocketBaseTagList(t);
    tr->tr_node.io_Command=TR_ADDREQUEST; tr->tr_time.tv_secs=(ULONG)(ms/1000); tr->tr_time.tv_micro=(ULONG)(ms%1000)*1000; SendIO((struct IORequest*)tr); }
  WHERE("connecting to the host");
  int r=connect(s,(struct sockaddr*)a,sizeof *a);
  if(topen){
    if(r<0 && CheckIO((struct IORequest*)tr)){ *timed=1; g_cool=1; g_cool_until=ms_now()+COOL_MS; }
    if(!CheckIO((struct IORequest*)tr)) AbortIO((struct IORequest*)tr);
    WaitIO((struct IORequest*)tr);
    struct TagItem t[2]; t[0].ti_Tag=SBTM_SETVAL(SBTC_BREAKMASK); t[0].ti_Data=SIGBREAKF_CTRL_C; t[1].ti_Tag=TAG_END; t[1].ti_Data=0; SocketBaseTagList(t);
    SetSignal(0,bit); CloseDevice((struct IORequest*)tr); }
  if(tr) DeleteIORequest((struct IORequest*)tr);
  if(mp) DeleteMsgPort(mp);
  return r; }
#include "asb_ash.inc"
/* 0.84 Sound sync: +50 = the sound comes out later than AmiSurf thinks (hold the picture back more), 0 = reset.
   Saved in PROGDIR:Data/AmiSurf.prefs as AVSYNC= (an AVSYNC tooltype still wins at the next start). */
static void avsync_nudge(int d){
  avsync_set(d?g_avsync_user+d:0); g_av_sent=-1000;   /* resend avdelay at the next tick */
  data_dir(); prefs_write(PREF_FILE);
  char m[120]; snprintf(m,sizeof m,"Sound sync %+d ms (%s %+d, yours %+d): picture %s",av_out_ms(),g_av_emu==2?"Amiberry":"emulator",
    g_av_outlat>0?g_av_outlat:0,g_avsync_user,av_out_ms()>0?"waits for late sound":av_out_ms()<0?"shown sooner":"as measured");
  set_status(m); asb_log("av: %s",m); }
static void avsync_menu(void){
  for(;;){ char b[400];
    snprintf(b,sizeof b,"Sound and picture out of step?\n\nNow: %+d ms  (%s: %d ms after the audio device,\nyour correction %+d ms)\n\n"
      "Sound comes LATE (lips move first): Delay picture +50\nSound comes EARLY: Picture sooner -50\nSaved for next time.",
      av_out_ms(),g_av_emuname[0]?g_av_emuname:"sound not started",g_av_outlat>0?g_av_outlat:0,g_avsync_user);
    LONG r=ask("AmiSurf - Sound sync","Delay picture +50|Picture sooner -50|Reset|OK",b);
    if(r==1) avsync_nudge(50); else if(r==2) avsync_nudge(-50); else if(r==3) avsync_nudge(0); else break; } }
/* blocking with limits: start-up and the engine auto-start */
static int connect_host0(void);
static int connect_host(void){ target_pick(); CRUMB("connect %s:%d",g_th,g_tp); WHERE("connecting to the host"); asb_log("connecting to %s:%d",g_th,g_tp); ULONG t0=ms_now(); int r=connect_host0();
  if(r) asb_log("connect failed after %lu ms: %s",(unsigned long)(ms_now()-t0),g_status); return r; }
static int connect_host0(void){
  if(g_fd>=0){ CloseSocket(g_fd); g_fd=-1; }
  struct sockaddr_in a; if(resolve_host(&a)) return -1;
  int s=socket(AF_INET,SOCK_STREAM,0); if(s<0){ snprintf(g_status,sizeof g_status,"no socket"); return -1; }
  int timed=0;
  g_tcpfail=0;
  if(connect_timed(s,&a,CONNECT_MS,&timed)<0){ CloseSocket(s); g_tcpfail=1; snprintf(g_status,sizeof g_status,timed?"no answer from %s:%d":"cannot connect %s:%d",g_th,g_tp);
    if(!g_legacy && g_tsrc==2) g_disc_due=1;   /* nothing on 127.0.0.1:9220: search the LAN next */
    if(!g_legacy && g_tsrc==3) g_dh[0]=0;
    return -1; }
  int hs=handshake_to(s,HANDSHAKE_MS);
  if(hs){ CloseSocket(s); snprintf(g_status,sizeof g_status,"%s:%d %s",g_th,g_tp,hs_why(hs)); ash_note(hs); return -1; }
  connected_ok(s);
  return 0;
}
/* ---- offline: reconnect without blocking the window. A small state machine polled from the main loop (zero
   timeouts only): connect (blocking, <= 3 s) -> handshake (4 s, polled) -> connected; failures back off. ---- */
static int g_cs=-1, g_cstate, g_coff, g_clen; static ULONG g_ct0; static unsigned char g_cb[400];
static void rc_abort(void){ if(g_cs>=0){ CloseSocket(g_cs); g_cs=-1; } disc_stop(); g_cstate=0; }
static void rc_fail(const char *why){
  rc_abort();
  if(why) snprintf(g_status,sizeof g_status,"%.200s",why);
  asb_log("connect failed after %lu ms: %s",(unsigned long)(ms_now()-g_ct0),g_status);
  g_rc_fail++; long d=2000L<<(g_rc_fail>5?4:g_rc_fail-1); if(d>30000) d=30000;
  if(cool_left()>d) d=cool_left()+100;
  g_rc_next=ms_now()+(ULONG)d;
  char t[sizeof g_status]; snprintf(t,sizeof t,"%.150s - retrying in %ld s",g_status,d/1000); scopy_u(g_status,sizeof g_status,t);
  draw_status(); }
static void reconnect_poll(void){
  if(g_fd>=0 || g_replaced){ if(g_cstate) rc_abort(); return; }
  if(!g_cstate) ash_notice_poll();
  ULONG now=ms_now(); char why[300];
  if(g_cstate==10){   /* LAN search running */
    int r=disc_poll(); if(!r) return;
    g_cstate=0;
    if(r>0){ g_rc_now=1; g_rc_fail=0; }
    else { snprintf(why,sizeof why,"No AmiStudio Host found (127.0.0.1:%d, LAN search)",ASH_PORT); rc_fail(why); nohost_notice(); return; } }
  if(!g_cstate && g_disc_due){ g_disc_due=0; g_ct0=now;
    snprintf(g_status,sizeof g_status,"searching the network for an AmiStudio Host ..."); draw_status();
    if(disc_start()==0){ g_cstate=10; return; }
    snprintf(why,sizeof why,"No AmiStudio Host at 127.0.0.1:%d (LAN search failed)",ASH_PORT); rc_fail(why); nohost_notice(); return; }
  if(!g_cstate){
    if(g_rc_now && cool_left()>0){   /* Settings changed right after a host that did not answer: wait for that attempt to end */
      static ULONG lastshow; if((LONG)(now-lastshow)>=1000){ lastshow=now; snprintf(g_status,sizeof g_status,"waiting %ld s for the last connection attempt to end ...",cool_left()/1000+1); draw_status(); }
      return; }
    if(!g_rc_now && (LONG)(now-g_rc_next)<0) return;
    g_rc_now=0; g_ct0=now; target_pick();
    CRUMB("reconnect %s:%d",g_th,g_tp); asb_log("connecting to %s:%d",g_th,g_tp);
    snprintf(g_status,sizeof g_status,"connecting to %s:%d ...",g_th,g_tp); draw_status();
    struct sockaddr_in a; if(resolve_host(&a)){ rc_fail(NULL); return; }
    g_cs=socket(AF_INET,SOCK_STREAM,0); if(g_cs<0){ rc_fail("no socket"); return; }
    int timed=0;   /* the connect itself is quick (refused or accepted) except for a host that does not answer: <= 3 s */
    if(connect_timed(g_cs,&a,CONNECT_MS,&timed)<0){ snprintf(why,sizeof why,timed?"no answer from %s:%d":"cannot connect %s:%d",g_th,g_tp);
      if(!g_legacy && g_tsrc==3) g_dh[0]=0;
      if(!g_legacy && g_tsrc==2){ rc_abort(); snprintf(g_status,sizeof g_status,"%.200s",why); asb_log("connect failed after %lu ms: %s",(unsigned long)(ms_now()-g_ct0),why); g_disc_due=1; return; }
      rc_fail(why); return; }
    g_clen=hs_build(g_cb); g_coff=0; g_cstate=2; return; }
  long el=(long)(now-g_ct0);
  if(g_cstate==2){   /* send the hello, then read the 7-byte reply */
    if(el>CONNECT_MS+HANDSHAKE_MS){ snprintf(why,sizeof why,"%s:%d %s",g_th,g_tp,hs_why(-3)); rc_fail(why); return; }
    if(g_coff<g_clen){ long k=send(g_cs,g_cb+g_coff,g_clen-g_coff,0); if(k<=0){ snprintf(why,sizeof why,"%s:%d %s",g_th,g_tp,hs_why(-1)); rc_fail(why); return; } g_coff+=(int)k; if(g_coff<g_clen) return; g_coff=0; g_cstate=3; }
    else g_cstate=3; }
  if(g_cstate==3){
    if(el>CONNECT_MS+HANDSHAKE_MS){ snprintf(why,sizeof why,"%s:%d %s",g_th,g_tp,hs_why(-3)); rc_fail(why); return; }
    if(!wait_sock(g_cs,0,0)) return;
    long k=recv(g_cs,g_cb+g_coff,hs_need(g_cb,g_coff)-g_coff,0); if(k<=0){ snprintf(why,sizeof why,"%s:%d %s",g_th,g_tp,hs_why(-1)); ash_note(-1); rc_fail(why); return; }
    g_coff+=(int)k; if(g_coff<hs_need(g_cb,g_coff)) return;
    int hs=hs_parse(g_cb,g_coff);
    if(hs){ snprintf(why,sizeof why,"%s:%d %s",g_th,g_tp,hs_why(hs)); ash_note(hs); rc_fail(why); return; }
    g_note_shown=0; g_nohost_shown=0;
    int s=g_cs; g_cs=-1; g_cstate=0;
    connected_ok(s); draw(); } }
/* drop the connection and let reconnect_poll() connect again from the main loop (Settings changed) */
static void reconnect_soon(void){
  CRUMB("reconnect after settings change");
  rc_abort();
  if(g_fd>=0){ CloseSocket(g_fd); g_fd=-1; }
  g_replaced=0; g_rc_fail=0; g_rc_now=1; g_rs_host[0]=0; g_note_shown=0; g_nohost_shown=0; g_dh[0]=0;
  snprintf(g_status,sizeof g_status,"connecting to %s:%d ...",g_th,g_tp); draw_status(); }

static void url_from_input(const char *in,char *out,int cap){
  if(strstr(in,"://")||!strncmp(in,"about:",6)){ snprintf(out,cap,"%s",in); return; }
  int dot=0; for(const char *p=in;*p;p++) if(*p=='.') dot=1;
  if(dot && !strchr(in,' ')){ snprintf(out,cap,"https://%s",in); return; }
  snprintf(out,cap,"?%s",in); /* host substitutes its search engine (asb://settings) */
}

/* ---------- input (shared by the Intuition and MUI front ends; coordinates relative to OX/OY) ---------- */
static void ev_move(int mx,int my,UWORD qual){
          if(g_popn && g_popkind!=3){ int r=pop_hit(mx,my); if(r!=g_pophov){ g_pophov=r; pop_draw(); } if(r>=0) return; }
          if(my>=TOP && my<g_H-bot_h()){
            int held=!!(qual&IEQUALIFIER_LEFTBUTTON);
            static int skip; skip++;
            if(held || (skip&1)==0) send_mouse(mx,my,held?1:0,ASBC_MMOVE);
          }
        }
static void ev_button(UWORD code,int mx,int my){
          if(g_fd<0 && g_replaced && code==SELECTDOWN){ g_replaced=0; return; }
          if(g_debug){ char d[80]; snprintf(d,sizeof d,"btn %04x at %d,%d (TOP %d)",code,mx,my,TOP); set_status(d); }
          int up=!!(code&IECODE_UP_PREFIX);
          int ie=code&~IECODE_UP_PREFIX;
          int left=(code==SELECTDOWN||code==SELECTUP||ie==IECODE_LBUTTON);
          int right=(ie==IECODE_RBUTTON);
          int btn=left?1: (right?2:4);
#ifndef ASP_MUI
          static int dragtab=-1;
#endif
          static int popdown;
          if(g_popgone_s && !up && (left||right)){ ULONG s2,u2; CurrentTime(&s2,&u2); long ms=(long)(s2-g_popgone_s)*1000+((long)u2-(long)g_popgone_u)/1000; g_popgone_s=0;
            if(ms>=0 && ms<400){ popdown=1; if(!(right && my>=TOP && my<g_H-bot_h())) return; } }
          if(g_popn && !up && (left||right)){
            int r=pop_hit(mx,my);
            if(r>=0){ if(left && g_pop[r].id!=M_NONE){ int id=g_pop[r].id; char a[1200]; scopy_u(a,sizeof a,g_pop[r].arg); pop_close(); do_menu(id,a); popdown=1; } return; }
            asb_log("click outside menu at %d,%d",mx,my); pop_close(); popdown=1; if(!(right && my>=TOP && my<g_H-bot_h())) return;
          }
          if(up && popdown){ popdown=0; if(left) return; }
#ifndef ASP_MUI
          if(!left && !right && !up && g_bmh && my>=TABH+TOOLH && my<TOP){ gt_bm_click(mx,1); return; }   /* middle-click: new tab */
#endif
          if(right){
            if(!up && my>=TOP && my<g_H-bot_h() && g_fd>=0){ /* our own context menu; Chrome's native one would be invisible */
              g_ctxx=mx; g_ctxy=my;
              int cx=g_cssw>0?(int)((long)mx*g_cssw/g_W):mx, cy=g_cssh>0?(int)((long)(my-TOP)*g_cssh/content_h()):my-TOP;
              send_cmd("ctx %d %d",cx,cy); }
            return;
          }
          if(left && !up){
            if(my<0){ }
#ifndef ASP_MUI
            else if(my<TABH && !g_full){
              int n=g_ntab>0?g_ntab:1, tw=tab_w();
              int plus=4+n*tw;
              if(mx>=plus && mx<plus+24){ if(g_fd>=0) asbc_send(g_fd,ASBC_TABNEW,NULL,0); }
              else if(mx>=4){
                int i=(mx-4)/tw;
                if(i>=0 && i<n){
                  int local=mx-4-i*tw;
                  unsigned char ix=(unsigned char)i;
                  if(local>tw-16){ if(g_fd>=0) asbc_send(g_fd,ASBC_TABCLS,&ix,1); }
                  else dragtab=i; /* select on release; release on another tab moves it there */
                }
              }
            }
            else if(g_bmh && my>=TABH+TOOLH && my<TOP){ gt_bm_click(mx,qual_shift()); }
            else if(my<TOP){
              int b=tb_hit(mx,my); if(b>=0){ g_pressed=b; tb_draw(b); }
            }
#endif
            else if(my<g_H-bot_h()){
              url_unfocus();
              send_mouse(mx,my,btn,ASBC_MMOVE);
              send_mouse(mx,my,btn,ASBC_MDOWN);
            }
#ifndef ASP_MUI
            else if(g_find_on){
              int fy=g_H-bot_h()+2+g_stath+2-2;
              if(my>=fy && my<fy+g_stath+4){
                if(mx>=fb_x(0)&&mx<fb_x(0)+FB_BTN) send_cmd("findprev");
                else if(mx>=fb_x(1)&&mx<fb_x(1)+FB_BTN) send_cmd("findnext");
                else if(mx>=fb_x(2)&&mx<fb_x(2)+FB_BTN) find_show(0); }
            }
#endif
          } else if(left && up){
#ifndef ASP_MUI
            if(dragtab>=0){ int from=dragtab; dragtab=-1;
              int tw=tab_w(), n=g_ntab>0?g_ntab:1, to=(mx-4)/tw;
              if(my<TABH && mx>=4 && to>=0 && to<n && to!=from) send_cmd("tabmove %d %d",from,to);
              else { unsigned char ix=(unsigned char)from; if(g_fd>=0) asbc_send(g_fd,ASBC_TABSEL,&ix,1); } }
            else if(g_pressed>=0){ int b=g_pressed, hit=tb_hit(mx,my); g_pressed=-1; tb_draw(b);
              if(hit==b){ int ic=tb_icon(b);
                if(ic==IC_BACK){ if(g_fd>=0) asbc_send(g_fd,ASBC_BACK,NULL,0); }
                else if(ic==IC_FWD){ if(g_fd>=0) asbc_send(g_fd,ASBC_FWD,NULL,0); }
                else if(ic==IC_RELOAD){ if(g_fd>=0) asbc_send(g_fd,ASBC_RELOAD,NULL,0); }
                else if(ic==IC_STOP){ if(g_fd>=0) asbc_send(g_fd,ASBC_STOP,NULL,0); }
                else if(ic==IC_HOME) go_home();
                else if(ic==IC_STAR) bm_add();
                else if(ic==IC_BOOKMARKS) open_page("bookmarks");
                else if(ic==IC_HISTORY) open_page("history");
                else if(ic==IC_FIND) find_show(!g_find_on);
                else if(ic==IC_MENU) menu_main(tb_x(b)+BTN-10,TABH+TOOLH);
                else if(ic==IC_ABOUT) about_open(); } }
            else
#endif
            if(my>=TOP && my<g_H-bot_h()) send_mouse(mx,my,btn,ASBC_MUP);
          }
        }
static void ev_vanilla(UWORD code,UWORD qual){
          int shift=!!(qual&(IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT)), ctrl=!!(qual&IEQUALIFIER_CONTROL);
          int amig=!!(qual&(IEQUALIFIER_LCOMMAND|IEQUALIFIER_RCOMMAND));
          if(url_editing()) { }
          else if(code==27 && g_popn) pop_close();
          else if(amig){ int c=code; if(c>='A'&&c<='Z') c+=32; if(c=='{') c='['; if(c=='}') c=']'; shortcut(c,shift); }
          else if(code==9 && ctrl) send_cmd(shift?"tabprev":"tabnext");
          else if(code==12){ shortcut('l',0); }
          else if(code==6){ if(!g_find_on) find_show(1); else send_find(); }
          else if(code==7){ if(g_find_on) send_cmd(shift?"findprev":"findnext"); else find_show(1); }
          else if(code==18){ if(g_fd>=0) asbc_send(g_fd,ASBC_RELOAD,NULL,0); }
          else if(code==20){ if(g_fd>=0) asbc_send(g_fd,ASBC_TABNEW,NULL,0); }
          else if(code==23){ unsigned char ix=(unsigned char)g_tab; if(g_fd>=0) asbc_send(g_fd,ASBC_TABCLS,&ix,1); }
          else if(code==3) page_clip(0);
          else if(code==24) page_clip(1);
          else if(code==1) page_clip(2);
          else if(code==22) do_paste();
          else if(code==4) bm_add();
          else if(code==15) open_page("bookmarks");
          else if(code==25) open_page("history");
          else if(code==27){
            if(g_find_on) find_show(0);
            else if(g_full) full_toggle();
            else { send_key(ASBC_KDOWN,27,0,NULL); send_key(ASBC_KUP,27,0,NULL);   /* to the page: leaves video fullscreen, closes dialogs */
              if(g_loading && g_fd>=0) asbc_send(g_fd,ASBC_STOP,NULL,0); }
          }
          else if(code==8) send_key(ASBC_KDOWN,8,0,NULL);
          else if(code==13) send_key(ASBC_KDOWN,13,0,NULL);
          else if(code==9) send_key(ASBC_KDOWN,9,shift?8:0,NULL);
          else if(code>=32){ char c[2]={(char)code,0}; send_key(ASBC_KCHAR,0,0,c); }
        }
static void ev_rawkey(UWORD code,UWORD qual){
          int cmd=!!(qual&(IEQUALIFIER_LCOMMAND|IEQUALIFIER_RCOMMAND));
          int shift=!!(qual&(IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT));
          if(code==0x5F){ about_open(); }
          else if(code==0x59) full_toggle();               /* F10 */
          else if(cmd && code==0x10) g_quit=1;
          else if(url_editing()) {}
          else if(cmd && code==0x4F){ if(g_fd>=0) asbc_send(g_fd,ASBC_BACK,NULL,0); }
          else if(cmd && code==0x4E){ if(g_fd>=0) asbc_send(g_fd,ASBC_FWD,NULL,0); }
          else if(cmd && raw2char(code)) shortcut(raw2char(code),shift);
          else if(code==0x7A){ unsigned char b[2]; asbc_put_i16(b,-120); if(g_fd>=0) asbc_send(g_fd,ASBC_WHEEL,b,2); }
          else if(code==0x7B){ unsigned char b[2]; asbc_put_i16(b,120); if(g_fd>=0) asbc_send(g_fd,ASBC_WHEEL,b,2); }
          else if(code==0x4C) send_key(ASBC_KDOWN,38,0,NULL);
          else if(code==0x4D) send_key(ASBC_KDOWN,40,0,NULL);
          else if(code==0x4F) send_key(ASBC_KDOWN,37,0,NULL);
          else if(code==0x4E) send_key(ASBC_KDOWN,39,0,NULL);
}
/* once per main-loop pass */
static void ev_tail(void){
    if(g_pop_dirty){ g_pop_dirty=0; pop_draw(); }
    if(g_sugg_req<0){ g_sugg_req=0; if(g_popkind==3) pop_close(); }
    if(g_sugg_req>0 && url_editing()){ g_sugg_req=0; send_cmd("suggest %s",url_text()); }
    if(g_popkind==3 && !url_editing()) pop_close();
    field_clip_flush();
    if(g_pending_copy){ g_pending_copy=0; do_copy(); }
    if(g_pending_paste){ int w=g_pending_paste; g_pending_paste=0; if(w==2) find_paste(); else do_paste(); }
}

#ifdef ASP_MUI
#include "asb_mui.inc"
#else
static void about_open(void){ about_window(); draw(); }
#include "asb_rexx.inc"
/* the Workbench window was resized: new page area, gadgets moved, viewport sent so the host reflows the page */
static void relayout(void){
  g_bm_dirty=1;
  int nw=win->Width-win->BorderLeft-win->BorderRight, nh=win->Height-win->BorderTop-win->BorderBottom;
  if(nw==g_W && nh==g_H) return;
  pop_close();
  g_W=nw; g_H=nh;
  if(g_sg_ok && !g_full){ RemoveGadget(win,&g_sg);
    g_sg.Width=(WORD)(g_W-URLX-LOCKW-NRIGHT*BSTEP-10); if(g_sg.Width<40) g_sg.Width=40;
    AddGadget(win,&g_sg,~0); }
  if(g_find_on && g_fg_ok){ RemoveGadget(win,&g_fg); place_find_gadget(); AddGadget(win,&g_fg,~0); }
  free(g_scaled); g_scaled=NULL; g_sw=g_sh=0;
  fillp(0,0,g_W,g_H,tp[P_CANVAS]); RefreshWindowFrame(win);
  send_hello(); draw();
  if(g_sg_ok && !g_full) RefreshGList(&g_sg,win,NULL,1); }
static void url_focus(void){ if(g_sg_ok && !g_full){ g_omni_sel=1; g_url_clicks=0; ActivateGadget(&g_sg,win,NULL); } }
int main(int argc, char **argv){
  stack_mark(); store_migrate(); prefs_load(); win_box_load();
  tooltypes_load(argc,argv);
  if(argc>0){
    for(int i=1;i<argc;i++){
      if(!strcmp(argv[i],"-host")&&i+1<argc) snprintf(g_host,sizeof g_host,"%s",argv[++i]);
      else if(!strcmp(argv[i],"-port")&&i+1<argc) g_port=atoi(argv[++i]);
      else if(!strcmp(argv[i],"-token")&&i+1<argc) snprintf(g_token,sizeof g_token,"%s",argv[++i]);
      else if(strchr(argv[i],'=') && strncasecmp(argv[i],"http",4)){ char k[40]; const char *eq=strchr(argv[i],'='); int kl=(int)(eq-argv[i]); if(kl>39) kl=39; memcpy(k,argv[i],(size_t)kl); k[kl]=0; theme_tooltype(k,eq+1); }
      else if(!strcasecmp(argv[i],"MODEREQ")) theme_tooltype("MODEREQ",NULL);
      else if(!strcasecmp(argv[i],"STATS")) theme_tooltype("STATS",NULL);
      else if(argv[i][0]!='-'){ snprintf(g_url,sizeof g_url,"%s",argv[i]); g_url_given=1; }
    }
  }
  if(rexx_start()) return 0;   /* 0.85: another AmiSurf runs and took the URL */
  g_jbuf=malloc(g_jcap); if(!g_jbuf) return 20;
  IntuitionBase=(struct IntuitionBase*)OpenLibrary((STRPTR)"intuition.library",39);
  GfxBase=(struct GfxBase*)OpenLibrary((STRPTR)"graphics.library",39);
  if(!IntuitionBase||!GfxBase){ printf("needs OS 3.0+\n"); return 20; }
  P96Base=OpenLibrary((STRPTR)"Picasso96API.library",2);
  SocketBase=OpenLibrary((STRPTR)"bsdsocket.library",4);
  if(!SocketBase){ printf("needs bsdsocket.library (UAE bsdsocket_emu)\n"); return 20; }

  CyberGfxBase=OpenLibrary((STRPTR)"cybergraphics.library",40);
  if(screen_open()){ scr=LockPubScreen(NULL); if(!scr) return 20; g_ownscr=0; g_aga=0; }
  if(g_ownscr){
    win=OpenWindowTags(NULL,
      WA_Left,0, WA_Top,0, WA_Width,scr->Width, WA_Height,scr->Height,
      WA_CustomScreen,(ULONG)scr, WA_Backdrop,TRUE, WA_Borderless,TRUE,
      WA_Flags,WFLG_ACTIVATE|WFLG_SMART_REFRESH|WFLG_REPORTMOUSE|WFLG_RMBTRAP,
      WA_IDCMP,IDCMP_VANILLAKEY|IDCMP_RAWKEY|IDCMP_MOUSEBUTTONS|IDCMP_MOUSEMOVE|IDCMP_GADGETUP|IDCMP_GADGETDOWN,
      TAG_DONE);
  } else {
    int wx,wy,ww,wh; win_box(scr,&wx,&wy,&ww,&wh);
    win=OpenWindowTags(NULL,
      WA_Left,wx, WA_Top,wy, WA_Width,ww, WA_Height,wh,
      WA_Title,(ULONG)"AmiSurf",
      WA_PubScreen,(ULONG)scr,
      WA_MinWidth,320, WA_MinHeight,200, WA_MaxWidth,~0, WA_MaxHeight,~0,
      WA_Flags,WFLG_CLOSEGADGET|WFLG_DRAGBAR|WFLG_DEPTHGADGET|WFLG_SIZEGADGET|WFLG_SIZEBBOTTOM|WFLG_ACTIVATE|WFLG_SMART_REFRESH|WFLG_REPORTMOUSE|WFLG_RMBTRAP,
      WA_IDCMP,IDCMP_CLOSEWINDOW|IDCMP_NEWSIZE|IDCMP_REFRESHWINDOW|IDCMP_VANILLAKEY|IDCMP_RAWKEY|IDCMP_MOUSEBUTTONS|IDCMP_MOUSEMOVE|IDCMP_GADGETUP|IDCMP_GADGETDOWN,
      TAG_DONE);
  }
  if(!win){ if(g_ownscr) CloseScreen(scr); else UnlockPubScreen(NULL,scr); return 20; }
  RP=win->RPort; OX=win->BorderLeft; OY=win->BorderTop;
  g_W=win->Width-win->BorderLeft-win->BorderRight;
  g_H=win->Height-win->BorderTop-win->BorderBottom;
  detect_rtg();
  theme_pens();
  theme_font();
  g_tabh=RP->TxHeight+(g_large?16:10); if(g_tabh<20) g_tabh=20;
  if(g_large){ g_btn=40; g_bstep=42; g_toolh=48; }
  g_stath=RP->TxHeight+(g_large?10:6); if(g_stath<16) g_stath=16;
  gt_bm_height();
  icons_load();
  struct Window *splash=NULL; ULONG sp_s=0, sp_u=0;
  if(g_splash_ms>0){ char st[96]; target_pick(); snprintf(st,sizeof st,"Connecting to %s:%d ...",g_th,g_tp); splash=splash_open(st); CurrentTime(&sp_s,&sp_u); }
  SetAPen(RP,(ULONG)tp[P_CANVAS]); RectFill(RP,OX,OY,OX+g_W-1,OY+g_H-1);
  snprintf(g_status,sizeof g_status,"AmiSurf %s",ASB_VERSION);
  make_url_gadget();
  log_startup(); if(connect_host()!=0) autohost();
  if(splash){ ULONG s2,u2; for(;;){ CurrentTime(&s2,&u2); long ms=(long)(s2-sp_s)*1000+((long)u2-(long)sp_u)/1000; if(ms>=g_splash_ms) break; Delay(2); } splash_close(splash); }
  draw();

  ULONG winsig=1UL<<win->UserPort->mp_SigBit;
  int run=1;
  while(run){
    fd_set rf; FD_ZERO(&rf);
    ULONG sigs=winsig|audio_sigmask()|pop_win_sig()|rexx_sigmask();
    struct timeval tv; tv.tv_sec=0; tv.tv_usec=30000;
    int nf=0;
    if(g_fd>=0){ FD_SET(g_fd,&rf); nf=g_fd+1; }
    /* offline: UAE's WaitSelect() with no sockets ignores the timeout, so poll instead (reconnect every ~2 s) */
    loop_watch(); WHERE("wait");
    if(g_popwin) tv.tv_usec=0;   /* menu window open: poll (UAE's WaitSelect does not wake for its signal) */
    if(g_fd>=0){ int ns=WaitSelect(nf, &rf, NULL, NULL, &tv, &sigs); if(g_popwin && ns<=0) Delay(1); } else Delay(3);
    WHERE("audio"); stats_tick(); audio_service(); WHERE("menu window"); pop_win_poll(); menu_state_poll(); WHERE("input");
    {
      struct IntuiMessage *m;
      while((m=(struct IntuiMessage*)GetMsg(win->UserPort))){
        ULONG cl=m->Class; UWORD code=m->Code; UWORD qual=m->Qualifier;
        WORD mx=m->MouseX-OX, my=m->MouseY-OY;
        APTR iaddr=m->IAddress;
        ReplyMsg((struct Message*)m);
        if(cl==IDCMP_CLOSEWINDOW) run=0;
        else if(cl==IDCMP_NEWSIZE) relayout();
        else if(cl==IDCMP_REFRESHWINDOW){ BeginRefresh(win); EndRefresh(win,TRUE); }
        else if(cl==IDCMP_GADGETUP && iaddr==&g_sg){
          pop_close(); char u[2400]; url_from_input((char*)g_sbuf,u,sizeof u); send_nav(u);
          g_omni_sel=0; g_url_clicks=0; sync_url_gadget();
        } else if(cl==IDCMP_GADGETUP && iaddr==&g_fg) send_find();
        else if(cl==IDCMP_MOUSEMOVE) ev_move(mx,my,qual);
        else if(cl==IDCMP_MOUSEBUTTONS) ev_button(code,mx,my);
        else if(cl==IDCMP_VANILLAKEY) ev_vanilla(code,qual);
        else if(cl==IDCMP_RAWKEY && !(code&IECODE_UP_PREFIX)) ev_rawkey(code,qual);
      }
    }
    WHERE("arexx"); rexx_poll();
    if(g_bm_dirty){ g_bm_dirty=0; gt_bm_relayout(); }
    if(g_quit) run=0;
    ev_tail();
    if(g_fd>=0 && nf && FD_ISSET(g_fd,&rf)){ WHERE("receiving from host"); pump(); }
    else if(g_fd<0){
      reconnect_poll();
    }
  }
  pop_win_close();
  rc_abort(); if(g_fd>=0) CloseSocket(g_fd);
  STKLOG("quit"); asb_log("quit"); audio_close(); rexx_end(); qual_close(); bm_free();
  win_box_save(win);
  free(g_scaled);
  if(g_sg_ok && !g_full) RemoveGadget(win,&g_sg);
  if(g_find_on) RemoveGadget(win,&g_fg);
  if(GadToolsBase) CloseLibrary(GadToolsBase);
  CloseWindow(win); theme_pens_free(); icons_free(); free(g_chunky);
  if(g_font) CloseFont(g_font);
  if(g_ownscr) CloseScreen(scr); else UnlockPubScreen(NULL,scr);
  if(AslBase) CloseLibrary(AslBase);
  if(DiskfontBase) CloseLibrary(DiskfontBase);
  if(g_tmr) CloseDevice((struct IORequest*)&g_tr);
  if(P96Base) CloseLibrary(P96Base);
  if(CyberGfxBase) CloseLibrary(CyberGfxBase);
  CloseLibrary(SocketBase);
  CloseLibrary((struct Library*)GfxBase);
  CloseLibrary((struct Library*)IntuitionBase);
  return 0;
}
#endif /* !ASP_MUI */
