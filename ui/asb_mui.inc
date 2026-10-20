/* AmiSurf — MUI front end (0.70). Included by asb_gfx_chrome.c when built with -DASP_MUI.
 * Shared with the GadTools/Intuition client: protocol, frames (raw/RLE/QOI/JPEG), scrolling, popups (main menu,
 * context menu, address suggestions), shortcuts, stats, settings storage. MUI owns the chrome: tab strip, toolbar,
 * address string, find bar, status bar, menus, Settings and About. The page is a custom Area subclass; TOP is 0
 * and OX/OY/g_W/g_H describe that object, so every shared drawing routine renders into it. */
#define MUIA_IconBtn_Icon (TAG_USER|0x41530010)
#ifndef _isinobject
#define _asb_between(a,x,b) ((x)>=(a)&&(x)<=(b))
#define _isinobject(o,x,y) (_asb_between(_mleft(o),(x),_mright(o))&&_asb_between(_mtop(o),(y),_mbottom(o)))
#endif
enum { RID_CLOSE=0x40ff, RID_URL=0x4100, RID_URLCHG, RID_FIND, RID_FINDPREV, RID_FINDNEXT, RID_FINDCLOSE, RID_NEWTAB,
       RID_SET_SAVE, RID_SET_USE, RID_SET_CANCEL,
       RID_TAB0=0x4200, RID_TABX0=0x4300, RID_TB0=0x4400, RID_MENU0=0x4500,
       RID_BMK0=0x4600, RID_BMF0=0x4700, RID_BMB0=0x4780, RID_BMMORE=0x47F0 };   /* 0.87: bookmark i, folder f, bar button k, bar \xBB */
#define NTB 9
static Object *m_findbar, *m_findcnt, *m_status, *m_info, *m_top, *m_statbar, *m_lock;
static Object *m_tab[MAXTAB], *m_tabx[MAXTAB], *m_tabg[MAXTAB], *m_tb[NTB];
static Object *m_setwin, *m_s_host, *m_s_port, *m_s_token, *m_s_home, *m_s_theme, *m_s_font, *m_s_uif, *m_s_tool, *m_s_mode, *m_s_br;
static struct MUI_CustomClass *m_pagecl, *m_iconcl;
static unsigned long long g_esc_us;
static int g_pagedown, g_rmbtrap=-1, g_omni, g_omni_base;   /* g_omni: address just focused, the first key replaces it (omnibox) */
struct pagedata { struct MUI_EventHandlerNode eh; int lw, lh; };
struct icondata { int ic; };
static const int tb_ic[NTB]={IC_BACK,IC_FWD,IC_RELOAD,IC_HOME,IC_STAR,IC_BOOKMARKS,IC_HISTORY,IC_FIND,IC_MENU};
static const char *tb_help[NTB]={"Back (Amiga [)","Forward (Amiga ])","Reload (Amiga R) / Stop (Esc)","Home (Amiga H)","Bookmark this page (Amiga D)",
  "Bookmarks (Amiga O)","History (Amiga Y)","Find in page (Amiga F)","Menu"};

#ifdef ASB_TRACE
static void TR(const char *m){ FILE *f=fopen("SYS:ASB/trace.txt","a"); if(f){ fprintf(f,"%s\n",m); fclose(f); } }
#else
#define TR(m) ((void)0)
#endif
static ULONG xget(Object *o,ULONG a){ ULONG v=0; if(o) get(o,a,&v); return v; }
static void set_text(Object *o,char *last,size_t cap,const char *s){ /* only touch MUI when the text changed (relayout is slow on an 020) */
  if(!o||!strcmp(last,s)) return; scopy_u(last,cap,s); set(o,MUIA_Text_Contents,(ULONG)last); }

/* ---- chrome updates called by the shared code ---- */
static int url_editing(void){ return m_win && m_url && (Object*)xget(m_win,MUIA_Window_ActiveObject)==m_url; }
static int find_editing(void){ return m_win && m_find && (Object*)xget(m_win,MUIA_Window_ActiveObject)==m_find; }
static const char *url_text(void){ const char *u=(const char*)xget(m_url,MUIA_String_Contents); return u?u:""; }
static void url_unfocus(void){ if(m_win && m_page && (url_editing()||find_editing())) set(m_win,MUIA_Window_ActiveObject,(ULONG)m_page); }
static void url_focus(void){ if(!g_full && m_win){ set(m_win,MUIA_Window_ActiveObject,(ULONG)m_url); set(m_url,MUIA_String_BufferPos,(ULONG)strlen(url_text())); g_omni_base=(int)strlen(url_text()); g_omni=1; } }
static int sugg_x(void){ if(!m_url||!m_page) return 0; int x=_mleft(m_url)-_mleft(m_page); return x<0?0:x; }
static int sugg_w(void){ return m_url?_mwidth(m_url):g_W/2; }
static void sync_url_gadget(void){
  if(!m_url || url_editing()) return;
  if(strcmp(url_text(),g_url)) nnset(m_url,MUIA_String_Contents,(ULONG)g_url); }
static void icon_set(Object *o,int ic){
  if(!o) return; struct icondata *d=INST_DATA(m_iconcl->mcc_Class,o);
  if(d->ic!=ic){ d->ic=ic; MUI_Redraw(o,MADF_DRAWOBJECT); } }
static void tb_draw(int slot){ if(slot==2) icon_set(m_tb[2],g_loading?IC_STOP:IC_RELOAD); }
static void draw_status(void){
  static char ls[200], li[160], lc[40];
  if(!m_status) return;
  set_text(m_status,ls,sizeof ls,g_status[0]?g_status:g_title);
  char r[160]; char z[16]=""; if(g_zoom!=100) snprintf(z,sizeof z,"%d%%  ",g_zoom);
  if(g_stats) snprintf(r,sizeof r,"%s%s  %s %dx%d",z,g_stxt[0]?g_stxt:"stats...",g_rtg?"RTG":g_aga?"AGA":g_clut?"WB 8-bit":"WB",g_W,g_H); else
  snprintf(r,sizeof r,"%s%s%s %dx%d",z,g_fd>=0?"":"offline  ",g_rtg?"RTG":g_aga?"AGA 256":g_clut?"WB 8-bit":"WB",g_W,g_H);
  set_text(m_info,li,sizeof li,r);
  if(g_find_on){ char c[40]; const char *q=(const char*)xget(m_find,MUIA_String_Contents);
    if(g_findtot) snprintf(c,sizeof c,"%d of %d",g_findcur,g_findtot); else snprintf(c,sizeof c,"%s",q&&q[0]?"no matches":"");
    set_text(m_findcnt,lc,sizeof lc,c); } }
static void draw_chrome(void){
  static char lt[MAXTAB][40]; static int lshow[MAXTAB]={1}, lsel[MAXTAB]={1};
  if(!m_top) return;
  int n=g_ntab>0?g_ntab:1;
  for(int i=0;i<MAXTAB;i++){
    int show=i<n; if(show!=lshow[i]){ lshow[i]=show; set(m_tabg[i],MUIA_ShowMe,show); }
    if(!show) continue;
    const char *s=(i<g_ntab && g_tabtitle[i][0])?g_tabtitle[i]:(i==g_tab&&g_title[0]?g_title:"New Tab");
    char t[40]; snprintf(t,sizeof t,"%.30s%s",s,strlen(s)>30?"...":"");
    set_text(m_tab[i],lt[i],sizeof lt[i],t);
    int sel=i==g_tab; if(sel!=lsel[i]||(int)xget(m_tab[i],MUIA_Selected)!=sel){ lsel[i]=sel; nnset(m_tab[i],MUIA_Selected,sel); } }
  icon_set(m_lock,is_secure()?IC_LOCK:IC_UNLOCK);
  tb_draw(2);
  sync_url_gadget();
  draw_status(); }
static void draw(void){ draw_chrome(); if(m_page && RP) MUI_Redraw(m_page,MADF_DRAWOBJECT); }
static void find_show(int on){
  if(!on && g_find_on) send_cmd("findclose");
  g_find_on=on; g_findcur=g_findtot=0;
  set(m_findbar,MUIA_ShowMe,on);
  if(on) set(m_win,MUIA_Window_ActiveObject,(ULONG)m_find);
  draw_status(); }
static void full_toggle(void){
  if(g_find_on) find_show(0);
  pop_close();
  g_full=!g_full;
  set(m_top,MUIA_ShowMe,!g_full); set(m_statbar,MUIA_ShowMe,!g_full);
  if(!g_full) snprintf(g_status,sizeof g_status,"Full screen off");
  draw_status(); }
static void do_copy(void){
  if(url_editing()){ const char *u=url_text(); clip_write(u,(int)strlen(u)); set_status("copied address"); }
  else page_clip(0); }
/* insert clipboard text at a MUI string's cursor */
static void mui_insert(Object *o,const char *t){
  const char *c=(const char*)xget(o,MUIA_String_Contents); if(!c) c="";
  int cur=(int)strlen(c), pos=(int)xget(o,MUIA_String_BufferPos), n=(int)strlen(t);
  char nb[2400]; if(pos<0||pos>cur) pos=cur; if(cur+n>(int)sizeof nb-1) n=(int)sizeof nb-1-cur; if(n<=0) return;
  memcpy(nb,c,(size_t)pos); memcpy(nb+pos,t,(size_t)n); strcpy(nb+pos+n,c+pos);
  set(o,MUIA_String_Contents,(ULONG)nb); set(o,MUIA_String_BufferPos,(ULONG)(pos+n)); }
static void do_paste(void){
  char buf[4000];
  if(url_editing()){ if(clip_read_line(buf,sizeof buf)>0){ if(g_omni){ g_omni=0; nnset(m_url,MUIA_String_Contents,(ULONG)""); } mui_insert(m_url,buf); } return; }
  int n=clip_read(buf,sizeof buf);
  if(n<=0){ set_status("clipboard empty"); return; }
  if(g_fd>=0) asbc_send(g_fd,ASBC_PASTE,buf,(uint32_t)n); }
static void find_paste(void){ char buf[400]; if(m_find && clip_read_line(buf,sizeof buf)>0) mui_insert(m_find,buf); }
static void about_open(void){
  char host[96]; snprintf(host,sizeof host,"%s %s:%d %s",g_legacy?"Engine":"AmiStudio Host",g_th,g_tp,g_fd>=0?"connected":"not connected");
  ULONG args[3]={(ULONG)host,(ULONG)(g_rtg?"RTG":g_aga?"AGA":"Workbench"),
#ifdef ASP_020
    (ULONG)"68020+FPU"
#else
    (ULONG)"68040"
#endif
  };
  CRUMB("about: MUI requester open");
  /* the requester is modal: nothing reads the socket until it closes. Stop the sound stream meanwhile so data does not
     pile up in the TCP stack (a beta tester's 0.80 freeze came right after picking About; not reproduced in FS-UAE) */
  int snd=g_fd>=0 && !g_mute; if(snd){ unsigned char m=0; asbc_send(g_fd,ASBC_AUDIOCTL,&m,1); }
  MUI_RequestA(m_app,m_win,0,(STRPTR)"About AmiSurf " ASB_VERSION,(STRPTR)"*_OK",
    (STRPTR)"\33c\33bAmiSurf " ASB_VERSION "\33n\n\nPages rendered by Chrome or Firefox on the host\n(asb-chromed), shown here over ASBC.\n\n%s\nMUI front end, %s screen, %s build\n\nIcons: Ken's Icons (Kenneth's DualPNG icons,\nalso shipped in AmiKit)\nToolbar icon shapes: Lucide (ISC)\nstb_image, QOI - see ATTRIBUTIONS",args);
  if(snd && g_fd>=0){ unsigned char m=1; asbc_send(g_fd,ASBC_AUDIOCTL,&m,1); }
  CRUMB("about: closed"); STKLOG("about"); }

/* ---- Settings window (same fields as the GadTools one) ---- */
static STRPTR cy_theme[]={(STRPTR)"System (MUI/WB pens)",(STRPTR)"Pro (dark)",NULL};
static STRPTR cy_font[]={(STRPTR)"Workbench font",(STRPTR)"Topaz 8",NULL};
static STRPTR cy_tool[]={(STRPTR)"Normal",(STRPTR)"Large",NULL};
static STRPTR cy_mode[]={(STRPTR)"Workbench window",(STRPTR)"Full screen (RTG, else AGA)",(STRPTR)"Full screen, ask mode",NULL};
static STRPTR cy_br[]={(STRPTR)"Host default",(STRPTR)"Chrome",(STRPTR)"Firefox",NULL};
static Object *sbtn(const char *t,const char *help){
  return TextObject, ButtonFrame, MUIA_Background,MUII_ButtonBack, MUIA_Font,MUIV_Font_Button, MUIA_InputMode,MUIV_InputMode_RelVerify,
    MUIA_Text_PreParse,(ULONG)"\33c", MUIA_Text_Contents,(ULONG)t, MUIA_Text_SetMax,TRUE, MUIA_ShortHelp,(ULONG)help, End; }
static Object *lab(const char *t){ return Label2((STRPTR)t); }
static Object *str(int max,int secret){ return StringObject, StringFrame, MUIA_String_MaxLen,max, MUIA_String_Secret,secret, MUIA_CycleChain,1, End; }
static Object *cyc(STRPTR *l){ return CycleObject, MUIA_Cycle_Entries,(ULONG)l, MUIA_CycleChain,1, End; }
static void settings_open(void){
  CRUMB("settings window open");
  if(!m_setwin){
    Object *bs,*bu,*bc;
    m_setwin=WindowObject, MUIA_Window_Title,(ULONG)"AmiSurf Settings", MUIA_Window_Screen,(ULONG)scr,
      MUIA_Window_RefWindow,(ULONG)m_win, MUIA_Window_LeftEdge,MUIV_Window_LeftEdge_Centered, MUIA_Window_TopEdge,MUIV_Window_TopEdge_Centered,
      WindowContents,(ULONG)(VGroup,
        Child,(ULONG)(ColGroup(2), GroupFrameT("Host and pages"),
          Child,(ULONG)lab("Host"), Child,(ULONG)(m_s_host=StringObject, StringFrame, MUIA_String_MaxLen,63, MUIA_CycleChain,1, MUIA_ShortHelp,(ULONG)"AmiStudio Host (PC name or address[:port]). Empty: the AmiStudio Host file (AmiStudio Prefs/Host or Data/Host), this PC (127.0.0.1:9220), then a LAN search", End),
          Child,(ULONG)lab("Port"), Child,(ULONG)(m_s_port=StringObject, StringFrame, MUIA_String_MaxLen,6, MUIA_String_Accept,(ULONG)"0123456789", MUIA_CycleChain,1, MUIA_ShortHelp,(ULONG)"Empty: 9220 (AmiStudio Host); 9223 with HOSTMODE=LEGACY", End),
          Child,(ULONG)lab("Token"), Child,(ULONG)(m_s_token=StringObject, StringFrame, MUIA_String_MaxLen,ASBC_MAX_TOKEN+1, MUIA_String_Secret,TRUE, MUIA_CycleChain,1, MUIA_ShortHelp,(ULONG)"Empty: the paired Host token (HostToken in AmiStudio Prefs/ or AmiSurf Data/)", End),
          Child,(ULONG)lab("Home page"), Child,(ULONG)(m_s_home=StringObject, StringFrame, MUIA_String_MaxLen,500, MUIA_CycleChain,1, MUIA_ShortHelp,(ULONG)"Empty: the home page set in the host's settings (menu: Host settings)", End),
          Child,(ULONG)lab("Browser"), Child,(ULONG)(m_s_br=cyc(cy_br)), End),
        Child,(ULONG)(ColGroup(2), GroupFrameT("Look (restart to apply)"),
          Child,(ULONG)lab("Theme"), Child,(ULONG)(m_s_theme=cyc(cy_theme)),
          Child,(ULONG)lab("Screen font"), Child,(ULONG)(m_s_font=cyc(cy_font)),
          Child,(ULONG)lab("UI font"), Child,(ULONG)(m_s_uif=str(60,0)),
          Child,(ULONG)lab("Toolbar"), Child,(ULONG)(m_s_tool=cyc(cy_tool)),
          Child,(ULONG)lab("Screen"), Child,(ULONG)(m_s_mode=cyc(cy_mode)), End),
        Child,(ULONG)(HGroup, MUIA_Group_SameSize,TRUE,
          Child,(ULONG)(bs=SimpleButton((STRPTR)"_Save")), Child,(ULONG)(bu=SimpleButton((STRPTR)"_Use")), Child,(ULONG)(bc=SimpleButton((STRPTR)"_Cancel")), End),
        End), End;
    if(!m_setwin){ CRUMB("settings window: could not be created"); set_status("Settings: could not create the window"); return; }
    CRUMB("settings window built");
    DoMethod(m_app,OM_ADDMEMBER,(ULONG)m_setwin);
    DoMethod(m_setwin,MUIM_Notify,MUIA_Window_CloseRequest,TRUE,(ULONG)m_app,2,MUIM_Application_ReturnID,RID_SET_CANCEL);
    DoMethod(bs,MUIM_Notify,MUIA_Pressed,FALSE,(ULONG)m_app,2,MUIM_Application_ReturnID,RID_SET_SAVE);
    DoMethod(bu,MUIM_Notify,MUIA_Pressed,FALSE,(ULONG)m_app,2,MUIM_Application_ReturnID,RID_SET_USE);
    DoMethod(bc,MUIM_Notify,MUIA_Pressed,FALSE,(ULONG)m_app,2,MUIM_Application_ReturnID,RID_SET_CANCEL); }
  char port[8], uif[64]=""; if(g_port) snprintf(port,sizeof port,"%d",g_port); else port[0]=0; if(g_uifont[0]) snprintf(uif,sizeof uif,"%s/%d",g_uifont,g_uisize);
  set(m_s_host,MUIA_String_Contents,(ULONG)g_host); set(m_s_port,MUIA_String_Contents,(ULONG)port);
  set(m_s_token,MUIA_String_Contents,(ULONG)g_token); set(m_s_home,MUIA_String_Contents,(ULONG)g_home);
  set(m_s_uif,MUIA_String_Contents,(ULONG)uif);
  set(m_s_theme,MUIA_Cycle_Active,g_theme_pro); set(m_s_font,MUIA_Cycle_Active,g_font_topaz); set(m_s_tool,MUIA_Cycle_Active,g_large);
  set(m_s_mode,MUIA_Cycle_Active,g_wbmode?0:g_modereq?2:1); set(m_s_br,MUIA_Cycle_Active,g_browser);
  set(m_setwin,MUIA_Window_Open,TRUE);
  CRUMB("settings window is open (%d)",(int)xget(m_setwin,MUIA_Window_Open)); STKLOG("settings"); }
static void settings_done(int act){
  if(!m_setwin) return;
  CRUMB("settings window %s",act==2?"Save":act==1?"Use":"Cancel");
  if(act){ const char *port=(const char*)xget(m_s_port,MUIA_String_Contents);
    settings_apply((const char*)xget(m_s_host,MUIA_String_Contents),port?atoi(port):0,(const char*)xget(m_s_token,MUIA_String_Contents),
      (const char*)xget(m_s_home,MUIA_String_Contents),(const char*)xget(m_s_uif,MUIA_String_Contents),
      (int)xget(m_s_theme,MUIA_Cycle_Active),(int)xget(m_s_font,MUIA_Cycle_Active),(int)xget(m_s_tool,MUIA_Cycle_Active),(int)xget(m_s_mode,MUIA_Cycle_Active),(int)xget(m_s_br,MUIA_Cycle_Active),act==2); }
  set(m_setwin,MUIA_Window_Open,FALSE); }

/* ---- icon button: a 24x24 toolbar icon (PROGDIR:Icons) on a MUI button frame ---- */
static ULONG SAVEDS ASM icon_dispatch(REG(a0,struct IClass *cl),REG(a2,Object *obj),REG(a1,Msg msg)){
  switch(msg->MethodID){
  case OM_NEW:{ obj=(Object*)DoSuperMethodA(cl,obj,msg); if(!obj) return 0;
    struct icondata *d=INST_DATA(cl,obj); d->ic=0; for(struct TagItem *t=((struct opSet*)msg)->ops_AttrList; t && t->ti_Tag!=TAG_DONE; t++) if(t->ti_Tag==MUIA_IconBtn_Icon) d->ic=(int)t->ti_Data; return (ULONG)obj; }
  case MUIM_AskMinMax:{ DoSuperMethodA(cl,obj,msg); struct MUI_MinMax *m=((struct MUIP_AskMinMax*)msg)->MinMaxInfo;
    int sz=IC_SZ+(g_large?12:4);
    m->MinWidth+=sz; m->DefWidth+=sz; m->MaxWidth+=sz; m->MinHeight+=sz; m->DefHeight+=sz; m->MaxHeight+=sz; return 0; }
  case MUIM_Draw:{ DoSuperMethodA(cl,obj,msg);
    if(!(((struct MUIP_Draw*)msg)->flags&(MADF_DRAWOBJECT|MADF_DRAWUPDATE))) return 0;
    struct icondata *d=INST_DATA(cl,obj); struct RastPort *srp=RP; int sx=OX, sy=OY;
    RP=_rp(obj); OX=_mleft(obj)+(_mwidth(obj)-IC_SZ)/2; OY=_mtop(obj)+(_mheight(obj)-IC_SZ)/2;
    int sel=(int)xget(obj,MUIA_Selected) || (d->ic==IC_FIND && g_find_on);
    if(sel){ SetAPen(RP,(ULONG)tp[P_ACCENT]); RectFill(RP,OX,OY,OX+IC_SZ-1,OY+IC_SZ-1); }
    icon_draw(d->ic,0,0,sel?P_ACCENT:P_CHROME,(d->ic==IC_UNLOCK));
    RP=srp; OX=sx; OY=sy; return 0; }
  }
  return DoSuperMethodA(cl,obj,msg); }
static Object *iconbtn(int ic,int input,const char *help){
  return (Object*)NewObject(m_iconcl->mcc_Class,NULL, MUIA_IconBtn_Icon,ic,
    input?MUIA_Frame:TAG_IGNORE,MUIV_Frame_ImageButton, MUIA_Background,MUII_BACKGROUND,
    MUIA_InputMode,input?MUIV_InputMode_RelVerify:MUIV_InputMode_None, MUIA_ShortHelp,(ULONG)help, TAG_DONE); }


/* ---- address/find strings: an edit hook (runs on input.device's task: only flags and the edit buffer here) ---- */
static volatile int g_field_esc, g_short_c, g_short_shift, g_omni_fix;
static ULONG field_edit_c(struct Hook *h, struct SGWork *sgw, ULONG *cmd){
  int isurl=h->h_Data!=NULL;
  if(!sgw||!cmd||*cmd!=SGH_KEY) return 0;
  struct InputEvent *ie=sgw->IEvent;
  UWORD q=ie?ie->ie_Qualifier:0, raw=ie?ie->ie_Code:0, ch=sgw->Code;
  if(raw&IECODE_UP_PREFIX) return 1;
#ifdef ASB_HB
  { extern volatile ULONG g_dbgk[16]; extern volatile int g_dbgn; if(g_dbgn<16) g_dbgk[g_dbgn++]=(ULONG)raw|((ULONG)ch<<8)|((ULONG)(q&0xff)<<16)|((ULONG)sgw->EditOp<<24); }
#endif
  int cmdk=!!(q&(IEQUALIFIER_LCOMMAND|IEQUALIFIER_RCOMMAND));
  if(raw==0x45 || (!ie && ch==27)){ g_field_esc=isurl?1:2; sgw->Actions=(sgw->Actions&~SGA_USE)|SGA_END; return 1; }   /* Esc leaves the field */
  { int r=field_clipkey(sgw,0,isurl?1:2);   /* Ctrl-C/X/V/A and the control-character filter; MUI keeps Amiga-C/X/V */
    if(r==2){ sgw->BufferPos=sgw->NumChars; if(isurl){ g_omni=2; g_omni_base=sgw->NumChars; } }
    if(r){ if(isurl && (sgw->Actions&SGA_USE)) g_omni=0; return 1; } }
  if(cmdk){ int k=raw2char(raw); if(k && !strchr("cvxaz",k)){ g_short_c=k; g_short_shift=!!(q&(IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT)); sgw->Actions&=~SGA_USE; return 1; } }
  if(!isurl) return 1;
  if(g_popkind==3 && g_popn && (raw==0x4D||raw==0x4C)){   /* cursor walks the suggestions */
    int n=g_popn, k=g_sugsel; k=raw==0x4D?(k+1)%n:(k<=0?n-1:k-1); g_sugsel=k;
    const char *u=g_pop[k].arg; int l=(int)strlen(u); if(l>sgw->StringInfo->MaxChars-1) l=sgw->StringInfo->MaxChars-1;
    memcpy(sgw->WorkBuffer,u,(size_t)l); sgw->WorkBuffer[l]=0; sgw->NumChars=(WORD)l; sgw->BufferPos=(WORD)l; g_omni=0;
    sgw->Actions=SGA_USE|SGA_REDISPLAY; g_pop_dirty=1; return 1; }
  if(raw!=0x4F && raw!=0x4E && raw!=0x44 && raw!=0x43) g_sugg_req=1;
  if(g_omni){   /* omnibox: the first key typed after focusing replaces the whole address (fixed up in field_poll) */
    if((sgw->EditOp==EO_INSERTCHAR||sgw->EditOp==EO_REPLACECHAR) && ch>=32){ g_omni=0; g_omni_fix=1; }
    else if(sgw->EditOp==EO_DELBACKWARD||sgw->EditOp==EO_DELFORWARD||sgw->EditOp==EO_CLEAR){   /* 0.77: clears all only after an explicit select-all (Ctrl-A); otherwise a normal one-character edit */
      if(g_omni==2){ g_omni_fix=2; sgw->Actions&=~SGA_USE; } g_omni=0; }
    else if(sgw->EditOp==EO_MOVECURSOR) g_omni=0; }
  return 1; }
static struct Hook g_urlhook, g_findhook;
static Object *field(int max,const char *help,struct Hook *hk,int isurl){
  memset(hk,0,sizeof *hk); hk->h_Entry=(HOOKFUNC)HookEntry; hk->h_SubEntry=(HOOKFUNC)field_edit_c; hk->h_Data=isurl?(APTR)1:NULL;
  return StringObject, StringFrame, MUIA_String_MaxLen,max, MUIA_CycleChain,1, MUIA_ShortHelp,(ULONG)help, MUIA_String_EditHook,(ULONG)hk, End; }
/* main-loop side of the hook's flags */
#ifdef ASB_HB
volatile ULONG g_dbgk[16]; volatile int g_dbgn;
#endif
static void field_poll(void){
#ifdef ASB_HB
  { int n=g_dbgn; for(int i=0;i<n;i++) asb_log("key raw %02lx ch %02lx q %02lx op %lu",g_dbgk[i]&0xff,(g_dbgk[i]>>8)&0xff,(g_dbgk[i]>>16)&0xff,g_dbgk[i]>>24); g_dbgn=0; }
#endif
  if(g_field_esc){ int w=g_field_esc; g_field_esc=0;
    if(w==2) find_show(0); else { pop_close(); g_sugg_req=0; g_omni=0; nnset(m_url,MUIA_String_Contents,(ULONG)g_url); }
    set(m_win,MUIA_Window_ActiveObject,(ULONG)m_page); }
  if(g_omni_fix){ int f=g_omni_fix; g_omni_fix=0; const char *u=url_text(); int l=(int)strlen(u);
    int n=l-g_omni_base, pos=(int)xget(m_url,MUIA_String_BufferPos); char t[2400]; t[0]=0;
    if(f==1 && n>0){ int st=pos-n; if(st<0||st+n>l) st=l-n; if(n>(int)sizeof t-1) n=(int)sizeof t-1; memcpy(t,u+st,(size_t)n); t[n]=0; }   /* keep only what was typed */
    nnset(m_url,MUIA_String_Contents,(ULONG)t); set(m_url,MUIA_String_BufferPos,(ULONG)strlen(t)); }
  if(g_ctrl_h){ int w=g_ctrl_h; g_ctrl_h=0; Object *o=w==2?m_find:m_url;   /* Ctrl-H = Backspace */
    if(o){ const char *c=(const char*)xget(o,MUIA_String_Contents); char t[2400]; if(!c) c="";
      int l=(int)strlen(c), pos=(int)xget(o,MUIA_String_BufferPos); if(l>(int)sizeof t-1) l=(int)sizeof t-1; if(pos<0||pos>l) pos=l;
      memcpy(t,c,(size_t)l); t[l]=0;
      if(w==1 && g_omni==2){ t[0]=0; pos=0; }
      else if(pos>0){ memmove(t+pos-1,t+pos,(size_t)(l-pos+1)); pos--; }
      if(w==1){ g_omni=0; g_sugg_req=1; }
      nnset(o,MUIA_String_Contents,(ULONG)t); set(o,MUIA_String_BufferPos,(ULONG)pos); } }
  if(g_short_c){ int c=g_short_c; g_short_c=0; shortcut(c,g_short_shift); }
  if(g_pop_dirty){ g_pop_dirty=0; pop_draw(); } }

/* ---- page: Chrome's pixels; mouse and keys go to the shared handlers ---- */
static void page_geom(Object *obj){ RP=_rp(obj); OX=_mleft(obj); OY=_mtop(obj); g_W=_mwidth(obj); g_H=_mheight(obj); if(g_font) SetFont(RP,g_font); }
static int rawkey_char(UWORD code,UWORD qual){
  if(!KeymapBase) return -1;
  struct InputEvent ie; memset(&ie,0,sizeof ie);
  ie.ie_Class=IECLASS_RAWKEY; ie.ie_Code=code; ie.ie_Qualifier=qual&~(IEQUALIFIER_LCOMMAND|IEQUALIFIER_RCOMMAND);
  char b[8]; LONG n=MapRawKey(&ie,(STRPTR)b,sizeof b,NULL);
  return n==1?(unsigned char)b[0]:-1; }
static ULONG page_event(Object *obj,struct MUIP_HandleEvent *msg){
  struct IntuiMessage *im=msg->imsg; if(!im||!RP) return 0;
  int in=_isinobject(obj,im->MouseX,im->MouseY);
  int mx=im->MouseX-_mleft(obj), my=im->MouseY-_mtop(obj);
  if(im->Class==IDCMP_MOUSEMOVE){
    struct Window *w=_window(obj);   /* right button: our context menu over the page, MUI menus over the chrome.
      MUI 5 (muimaster V21) owns WFLG_RMBTRAP and draws its menus itself (Paint 1.0.60); older MUI uses Intuition menus,
      so there the trap is set only while the pointer is over the page. */
    if(w && MUIMasterBase->lib_Version<21 && in!=g_rmbtrap){ g_rmbtrap=in; Forbid(); if(in) w->Flags|=WFLG_RMBTRAP; else w->Flags&=~WFLG_RMBTRAP; Permit(); }
    if(in||g_pagedown) ev_move(mx,my,im->Qualifier);
    return 0; }
  if(im->Class==IDCMP_MOUSEBUTTONS){
    int up=!!(im->Code&IECODE_UP_PREFIX);
    if(!up && !in){ if(g_popn) pop_close();
      /* 0.77: a click into the address just places the cursor (0.76 "selected all" invisibly, so Backspace wiped the field) */
      return 0; }
    if(up && !g_pagedown && !in) return 0;
    if(!up){ g_pagedown=1; if(url_editing()||find_editing()) set(_win(obj),MUIA_Window_ActiveObject,(ULONG)obj); }
    else g_pagedown=0;
    if(mx<0) mx=0; if(my<0) my=0; if(mx>=g_W) mx=g_W-1; if(my>=g_H) my=g_H-1;
    ev_button(im->Code,mx,my);
    return MUI_EventHandlerRC_Eat; }
  if(im->Class==IDCMP_RAWKEY){
    UWORD c=im->Code, q=im->Qualifier; if(c&IECODE_UP_PREFIX) return 0;
    if(c==0x45) g_esc_us=asb_us();   /* MUI also turns Esc into a close request: RID_CLOSE ignores that one */
    int cmd=!!(q&(IEQUALIFIER_LCOMMAND|IEQUALIFIER_RCOMMAND)), shift=!!(q&(IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT));
    int k=cmd?raw2char(c):0;
    if(url_editing()||find_editing()){
      if(url_editing() && g_popkind==3 && g_popn && (c==0x4D||c==0x4C)){   /* cursor walks the suggestions */
        int n=g_popn, s=g_sugsel; s=c==0x4D?(s+1)%n:(s<=0?n-1:s-1); g_sugsel=s;
        nnset(m_url,MUIA_String_Contents,(ULONG)g_pop[s].arg); pop_draw(); return MUI_EventHandlerRC_Eat; }
      if(c==0x45){   /* Esc: leave the field (MUI would turn it into a close request) */
        if(find_editing()) find_show(0); else { pop_close(); g_sugg_req=0; nnset(m_url,MUIA_String_Contents,(ULONG)g_url); }
        set(_win(obj),MUIA_Window_ActiveObject,(ULONG)obj); return MUI_EventHandlerRC_Eat; }
      if(c==0x5F){ about_open(); return MUI_EventHandlerRC_Eat; }
      if(c==0x59){ full_toggle(); return MUI_EventHandlerRC_Eat; }
      if(k && !strchr("cvxaz",k)){ shortcut(k,shift); return MUI_EventHandlerRC_Eat; }   /* MUI's string keeps the clipboard keys */
      return 0; }
    if(cmd && (c==0x4F||c==0x4E)){ ev_rawkey(c,q); return MUI_EventHandlerRC_Eat; }
    if(cmd && k){ shortcut(k,shift); return MUI_EventHandlerRC_Eat; }
    int ch=rawkey_char(c,q);
    if(ch>=0 && c!=0x46) ev_vanilla((UWORD)ch,q); else ev_rawkey(c,q);
    return MUI_EventHandlerRC_Eat; }
  return 0; }
static ULONG SAVEDS ASM page_dispatch(REG(a0,struct IClass *cl),REG(a2,Object *obj),REG(a1,Msg msg)){
  struct pagedata *d;
  switch(msg->MethodID){
  case MUIM_AskMinMax:{ DoSuperMethodA(cl,obj,msg); struct MUI_MinMax *m=((struct MUIP_AskMinMax*)msg)->MinMaxInfo;
    m->MinWidth+=160; m->DefWidth+=640; m->MaxWidth+=MUI_MAXMAX; m->MinHeight+=80; m->DefHeight+=400; m->MaxHeight+=MUI_MAXMAX; return 0; }
  case MUIM_Setup:
    if(!DoSuperMethodA(cl,obj,msg)) return FALSE;
    d=INST_DATA(cl,obj); memset(&d->eh,0,sizeof d->eh);
    d->eh.ehn_Object=obj; d->eh.ehn_Class=cl; d->eh.ehn_Priority=1;
    d->eh.ehn_Events=IDCMP_MOUSEBUTTONS|IDCMP_RAWKEY|IDCMP_MOUSEMOVE;
    DoMethod(_win(obj),MUIM_Window_AddEventHandler,(ULONG)&d->eh);
    return TRUE;
  case MUIM_Cleanup:
    d=INST_DATA(cl,obj); DoMethod(_win(obj),MUIM_Window_RemEventHandler,(ULONG)&d->eh);
    return DoSuperMethodA(cl,obj,msg);
  case MUIM_Show:{ ULONG r=DoSuperMethodA(cl,obj,msg); page_geom(obj); struct Window *w=_window(obj); if(w) ReportMouse(TRUE,w); return r; }
  case MUIM_Hide: RP=NULL; return DoSuperMethodA(cl,obj,msg);
  case MUIM_Draw:{
    DoSuperMethodA(cl,obj,msg);
    if(!(((struct MUIP_Draw*)msg)->flags&(MADF_DRAWOBJECT|MADF_DRAWUPDATE))) return 0;
    d=INST_DATA(cl,obj); page_geom(obj);
    if(d->lw!=g_W||d->lh!=g_H){ if(d->lw!=g_W) g_bm_dirty=1; d->lw=g_W; d->lh=g_H; if(g_fd>=0){ send_hello(); } draw_status(); }
    APTR clip=MUI_AddClipping(muiRenderInfo(obj),_mleft(obj),_mtop(obj),_mwidth(obj),_mheight(obj));
    int pw=g_nat&&g_natpf>=0?g_natw:(g_rgb?g_W:0), ph=g_nat&&g_natpf>=0?g_nath:(g_rgb?g_H:0);
    if(pw<g_W) fillp(pw,0,g_W-pw,g_H,tp[P_CANVAS]);
    if(ph<g_H) fillp(0,ph,pw<g_W?pw:g_W,g_H-ph,tp[P_CANVAS]);
    blit_page();
    if(g_popn) pop_draw();
    MUI_RemoveClipping(muiRenderInfo(obj),clip);
    return 0; }
  case MUIM_HandleEvent: return page_event(obj,(struct MUIP_HandleEvent*)msg);
  }
  return DoSuperMethodA(cl,obj,msg); }

/* ---- 0.87: bookmarks bar (MUI): NBMB text buttons, shown as many as fit, then "\xBB" (asb_bmarks.inc) ---- */
#define NBMB 24
static Object *m_bmbar, *m_bmb[NBMB], *m_bmmore, *m_bmnote, *m_mbmbar;
static char g_bmlab[NBMB][64];
static Object *bmbtn(const char *t,const char *help){
  return TextObject, ButtonFrame, MUIA_Background,MUII_ButtonBack, MUIA_Font,MUIV_Font_Button, MUIA_InputMode,MUIV_InputMode_RelVerify,
    MUIA_Text_Contents,(ULONG)t, MUIA_Text_SetMax,FALSE, MUIA_HorizWeight,0, MUIA_ShortHelp,(ULONG)help, MUIA_ShowMe,FALSE, End; }
static void bmbar_build(void){
  m_bmbar=HGroup, MUIA_Group_Spacing,2, MUIA_ShowMe,g_bmbar, End;
  for(int i=0;i<NBMB;i++){ m_bmb[i]=bmbtn("","Click: open. Shift-click: open in a new tab"); DoMethod(m_bmbar,OM_ADDMEMBER,(ULONG)m_bmb[i]); }
  m_bmnote=TextObject, MUIA_Text_Contents,(ULONG)"", MUIA_Text_SetMin,FALSE, MUIA_HorizWeight,100, End;
  DoMethod(m_bmbar,OM_ADDMEMBER,(ULONG)m_bmnote);
  m_bmmore=bmbtn("\xBB","More bookmarks"); DoMethod(m_bmbar,OM_ADDMEMBER,(ULONG)m_bmmore); }
static int g_bm_over=16, g_bm_measured;   /* button width minus its text width (frame + spacing), measured once shown */
static struct TextFont *g_bm_font;
static int mui_bm_measure(const char *l){
  struct RastPort rp; InitRastPort(&rp); if(g_bm_font) SetFont(&rp,g_bm_font);
  return TextLength(&rp,(STRPTR)l,(ULONG)strlen(l))+g_bm_over; }
static void mui_bm_relayout(void){
  if(!m_bmbar || !m_win || !xget(m_win,MUIA_Window_Open)) return;
  if(!g_bmbar){ set(m_bmbar,MUIA_ShowMe,FALSE); return; }
  g_bm_font=_font(m_bmmore);
  if(xget(m_bmb[0],MUIA_ShowMe) && _width(m_bmb[0])>0 && g_bmlab[0][0]){ struct RastPort rp; InitRastPort(&rp); if(g_bm_font) SetFont(&rp,g_bm_font);
    int o=_width(m_bmb[0])-TextLength(&rp,(STRPTR)g_bmlab[0],(ULONG)strlen(g_bmlab[0])); if(o>4 && o<60) g_bm_over=o+1; g_bm_measured=1; }
  int avail=_mwidth(m_top)-4;
  if(g_bm_known) bm_layout(avail,mui_bm_measure,2); else { g_nslot=g_nshown=0; g_morex=-1; }
  if(g_nshown>NBMB) g_nshown=NBMB;
  DoMethod(m_bmbar,MUIM_Group_InitChange);
  for(int k=0;k<NBMB;k++){
    int show=k<g_nshown || (!g_bm_known && k==0);
    if(show){ if(g_bm_known) slot_label(g_slot[k],g_bmlab[k],sizeof g_bmlab[k]); else scopy_u(g_bmlab[k],sizeof g_bmlab[k],"Bookmarks");
      set(m_bmb[k],MUIA_Text_Contents,(ULONG)g_bmlab[k]); }
    if((int)xget(m_bmb[k],MUIA_ShowMe)!=show) set(m_bmb[k],MUIA_ShowMe,show); }
  set(m_bmnote,MUIA_Text_Contents,(ULONG)(!g_bm_known?"\33r(the bookmarks bar needs the 0.87 engine)":!g_nslot?"No bookmarks yet: Amiga D or the star bookmarks this page":""));
  { int m=g_morex>=0; if((int)xget(m_bmmore,MUIA_ShowMe)!=m) set(m_bmmore,MUIA_ShowMe,m); }
  if(!xget(m_bmbar,MUIA_ShowMe)) set(m_bmbar,MUIA_ShowMe,TRUE);
  DoMethod(m_bmbar,MUIM_Group_ExitChange);
  asb_log("bookmarks bar: %d of %d shown%s",g_nshown,g_nslot,g_morex>=0?" + more":"");
  if(!g_bm_measured && g_nshown) g_bm_dirty=1; }   /* once more with the real button frame width */
static void bmbar_apply(void){
  if(m_mbmbar) nnset(m_mbmbar,MUIA_Menuitem_Checked,g_bmbar);
  if(m_bmbar) set(m_bmbar,MUIA_ShowMe,g_bmbar);
  g_bm_dirty=1; }

/* ---- menus ---- */
static Object *mi(const char *t,const char *key,int id){
  Object *o=MenuitemObject, MUIA_Menuitem_Title,(ULONG)t, key?MUIA_Menuitem_Shortcut:TAG_IGNORE,(ULONG)key, MUIA_UserData,id, End;
  return o; }
static Object *mbar(void){ return MenuitemObject, MUIA_Menuitem_Title,(ULONG)NM_BARLABEL, End; }
static Object *m_mstats;
static Object *build_menus(void){
  /* Amiga-key shortcuts listed here are taken by Intuition; T, [, ], G, 1-8 and the shifted ones stay with the page
     handler (it knows Shift); C/V/X/A stay with whichever string or page has the focus. Amiga+M switches screens. */
  return MenustripObject,
    MUIA_Family_Child,(ULONG)(MenuObject, MUIA_Menu_Title,(ULONG)"Project",
      MUIA_Family_Child,(ULONG)mi("New tab   (Amiga T)",NULL,M_NEWTAB),
      MUIA_Family_Child,(ULONG)mi("Reopen closed tab   (Shift Amiga T)",NULL,M_REOPEN),
      MUIA_Family_Child,(ULONG)mi("Close tab","W",M_CLOSETAB),
      MUIA_Family_Child,(ULONG)mbar(),
      MUIA_Family_Child,(ULONG)mi("Save page as PNG","S",M_SAVEPNG),
      MUIA_Family_Child,(ULONG)mi("Save page as HTML",NULL,M_SAVEHTML),
      MUIA_Family_Child,(ULONG)mi("Save page as PDF","P",M_SAVEPDF),
      MUIA_Family_Child,(ULONG)mi("Print (printer.device)",NULL,M_PRINT),
      MUIA_Family_Child,(ULONG)mi("View source","U",M_SOURCE),
      MUIA_Family_Child,(ULONG)mbar(),
      MUIA_Family_Child,(ULONG)mi("Settings...",",",M_SETTINGS),
      MUIA_Family_Child,(ULONG)mi("Host settings (home, search)",NULL,M_HOSTSET),
      MUIA_Family_Child,(ULONG)mi("Pair with Host...",NULL,M_PAIR),
      MUIA_Family_Child,(ULONG)mi("Forget Host token...",NULL,M_UNPAIR),
      MUIA_Family_Child,(ULONG)mi("MUI settings...",NULL,M_NUM+1),
      MUIA_Family_Child,(ULONG)mi("About...   (Help)","?",M_ABOUT),
      MUIA_Family_Child,(ULONG)mbar(),
      MUIA_Family_Child,(ULONG)mi("Quit","Q",M_QUIT), End),
    MUIA_Family_Child,(ULONG)(MenuObject, MUIA_Menu_Title,(ULONG)"Navigate",
      MUIA_Family_Child,(ULONG)mi("Back   (Amiga [)",NULL,M_BACK),
      MUIA_Family_Child,(ULONG)mi("Forward   (Amiga ])",NULL,M_FWD),
      MUIA_Family_Child,(ULONG)mi("Reload","R",M_RELOAD),
      MUIA_Family_Child,(ULONG)mi("Stop",".",M_STOP),
      MUIA_Family_Child,(ULONG)mi("Home","H",M_HOME),
      MUIA_Family_Child,(ULONG)mi("Open location","L",M_LOCATION),
      MUIA_Family_Child,(ULONG)mbar(),
      MUIA_Family_Child,(ULONG)mi("Bookmark this page","D",M_BOOKMARK),
      MUIA_Family_Child,(ULONG)mi("Bookmarks","O",M_BOOKMARKS),
      MUIA_Family_Child,(ULONG)mi("History","Y",M_HISTORY),
      MUIA_Family_Child,(ULONG)mi("Downloads","J",M_DOWNLOADS),
      MUIA_Family_Child,(ULONG)mi("Passwords",NULL,M_PASSWORDS), End),
    MUIA_Family_Child,(ULONG)(MenuObject, MUIA_Menu_Title,(ULONG)"View",
      MUIA_Family_Child,(ULONG)mi("Find in page","F",M_FIND),
      MUIA_Family_Child,(ULONG)mbar(),
      MUIA_Family_Child,(ULONG)mi("Zoom in","+",M_ZOOMIN),
      MUIA_Family_Child,(ULONG)mi("Zoom out","-",M_ZOOMOUT),
      MUIA_Family_Child,(ULONG)mi("Actual size","0",M_ZOOM0),
      MUIA_Family_Child,(ULONG)mi("Full screen   (F10)",NULL,M_FULL),
      MUIA_Family_Child,(ULONG)mi("Mute sound on/off","M",M_MUTE),
      MUIA_Family_Child,(ULONG)mi("Sound late: delay picture +50 ms",NULL,M_AVLATER),
      MUIA_Family_Child,(ULONG)mi("Sound early: picture -50 ms",NULL,M_AVEARLIER),
      MUIA_Family_Child,(ULONG)mi("Sound sync reset / show...",NULL,M_AVMENU),
      MUIA_Family_Child,(ULONG)(m_mbmbar=MenuitemObject, MUIA_Menuitem_Title,(ULONG)"Bookmarks bar", MUIA_Menuitem_Shortcut,(ULONG)"B", MUIA_Menuitem_Checkit,TRUE, MUIA_Menuitem_Toggle,TRUE, MUIA_UserData,M_BMBAR, End),
      MUIA_Family_Child,(ULONG)(m_mstats=MenuitemObject, MUIA_Menuitem_Title,(ULONG)"Speed stats", MUIA_Menuitem_Checkit,TRUE, MUIA_Menuitem_Toggle,TRUE, MUIA_UserData,M_STATS, End),
      MUIA_Family_Child,(ULONG)mbar(),
      MUIA_Family_Child,(ULONG)mi("Move tab left   (Shift Amiga [)",NULL,M_TABLEFT),
      MUIA_Family_Child,(ULONG)mi("Move tab right   (Shift Amiga ])",NULL,M_TABRIGHT), End),
    End; }
static void menus_notify(Object *strip){   /* each item returns RID_MENU0+id from MUIM_Application_NewInput */
  struct List *l=NULL; get(strip,MUIA_Family_List,&l); if(!l) return;
  APTR st=(APTR)l->lh_Head; Object *menu;
  while((menu=(Object*)NextObject((Object**)&st))){
    struct List *il=NULL; get(menu,MUIA_Family_List,&il); if(!il) continue;
    APTR is=(APTR)il->lh_Head; Object *o;
    while((o=(Object*)NextObject((Object**)&is))){
      ULONG id=xget(o,MUIA_UserData);
      if(id) DoMethod(o,MUIM_Notify,MUIA_Menuitem_Trigger,MUIV_EveryTime,(ULONG)m_app,2,MUIM_Application_ReturnID,RID_MENU0+id); } } }

/* ---- 0.79 hamburger menu. On MUI 5 (muimaster V20+) it is MUI's own popup menu, the way MUI 5 draws the menu
   strip (which worked on a beta tester's 3.2 machine where our borderless menu window strobed and crashed); older MUI keeps
   our own menu window. Either way it opens from the main loop, after MUI has finished with the button release. ---- */
static int g_hamb_req; static Object *m_hamb;
/* 0.87: the item picked in the MUI popup is run on the next main-loop pass (g_hamb_pick), not inside
   hamb_popup() right after MUIM_Menustrip_Popup returns, and the strip is disposed of only when the next one is
   built (or at quit), so nothing MUI may still hold from the menu is freed under it. */
static ULONG g_hamb_pick; static Object *m_hamb_old;
static void hamb_free(void){ if(m_hamb_old){ MUI_DisposeObject(m_hamb_old); m_hamb_old=NULL; } }
static void handle_rid(ULONG id);
static void hamb_label(const char *l,char *t,int tc,char *key){   /* "Zoom out             A--" -> "Zoom out" + key "-" */
  const char *sp=NULL, *q; key[0]=0;
  for(q=l; (q=strstr(q,"  ")); q++) sp=q;   /* the last run of spaces starts the shortcut column */
  if(!sp){ snprintf(t,(size_t)tc,"%s",l); return; }
  int e=(int)(sp-l); while(e>0 && l[e-1]==' ') e--;
  const char *k=sp; while(*k==' ') k++;
  if(k[0]=='A' && k[1]=='-' && k[2] && !k[3]){ key[0]=k[2]; key[1]=0; snprintf(t,(size_t)tc,"%.*s",e,l); }
  else if(!strncmp(k,"A-S-",4)) snprintf(t,(size_t)tc,"%.*s   (Shift Amiga %s)",e,l,k+4);
  else if(*k) snprintf(t,(size_t)tc,"%.*s   (%s)",e,l,k);
  else snprintf(t,(size_t)tc,"%.*s",e,l); }
/* 0.87: MUI menu rows from g_bmi (asb_bmarks.inc); a folder row (ud -1) gets the lvl 1 rows after it as a submenu.
   Titles must stay put while the strip lives: they are in g_bmt, rebuilt only after hamb_free(). */
static char g_bmt[BMI_MAX][80], g_bmk[BMI_MAX][2];
static Object *bmi_item(int i){
  if(g_bmi[i].ud==M_NONE && g_bmi[i].label[0]=='-') return MenuitemObject, MUIA_Menuitem_Title,(ULONG)NM_BARLABEL, End;
  if(g_bmi[i].ud>=UD_BM){ scopy_u(g_bmt[i],sizeof g_bmt[i],g_bmi[i].label); g_bmk[i][0]=0; }   /* a title, never a shortcut column */
  else hamb_label(g_bmi[i].label,g_bmt[i],sizeof g_bmt[i],g_bmk[i]);
  return MenuitemObject, MUIA_Menuitem_Title,(ULONG)g_bmt[i], g_bmk[i][0]?MUIA_Menuitem_Shortcut:TAG_IGNORE,(ULONG)g_bmk[i],
    MUIA_Menuitem_Enabled,g_bmi[i].ud!=M_NONE, MUIA_UserData,g_bmi[i].ud>0?g_bmi[i].ud:0, End; }
static void bmi_fill(Object *parent){
  Object *sub=NULL;
  for(int i=0;i<g_nbmi;i++){ Object *o=bmi_item(i); if(!o) continue;
    if(g_bmi[i].lvl && sub) DoMethod(sub,OM_ADDMEMBER,(ULONG)o);
    else { DoMethod(parent,OM_ADDMEMBER,(ULONG)o); sub=g_bmi[i].ud==-1?o:NULL; } } }
static ULONG ud_rid(ULONG r){
  if(r>=UD_BMF && r<UD_BMF+BMFMAX) return RID_BMF0+(r-UD_BMF);
  if(r>=UD_BM && r<UD_BM+BMMAX) return RID_BMK0+(r-UD_BM);
  if(r>0 && r<M_NUM) return RID_MENU0+r;
  return 0; }
static int hamb_popup(void){
#ifdef ASB_OWNMENU
  return 0;   /* (test build: our own menu window on MUI 5 too) */
#endif
  if(!MUIMasterBase || MUIMasterBase->lib_Version<20 || !m_tb[8]) return 0;
  hamb_free();   /* last time's strip (kept until now) */
  pop_close(); g_popn=0; menu_main_items(); int n=g_popn;
  static char titles[POPMAX][64], keys[POPMAX][2];
  Object *menu=MenuObject, MUIA_Menu_Title,(ULONG)"AmiSurf", End;
  if(!menu){ g_popn=0; return 0; }
  for(int i=0;i<n;i++){ Object *o;
    if(g_pop[i].id==M_BMMENU){ o=MenuitemObject, MUIA_Menuitem_Title,(ULONG)"Bookmarks", End;   /* 0.87: submenu */
      if(o){ bmi_menu(1); bmi_fill(o); } }
    else if(g_pop[i].id==M_NONE) o=MenuitemObject, MUIA_Menuitem_Title,(ULONG)NM_BARLABEL, End;
    else { hamb_label(g_pop[i].label,titles[i],sizeof titles[i],keys[i]);
      o=MenuitemObject, MUIA_Menuitem_Title,(ULONG)titles[i], keys[i][0]?MUIA_Menuitem_Shortcut:TAG_IGNORE,(ULONG)keys[i],
        MUIA_UserData,g_pop[i].id, End; }
    if(o) DoMethod(menu,OM_ADDMEMBER,(ULONG)o); }
  g_popn=0; g_popkind=0;
  m_hamb=MenustripObject, MUIA_Family_Child,(ULONG)menu, End;
  if(!m_hamb){ MUI_DisposeObject(menu); return 0; }
  Object *b=m_tb[8]; LONG x=_mleft(b), y=_mbottom(b)+1;
  CRUMB("menu 1 (MUI popup): %d items at %ld,%ld",n,(long)x,(long)y);
  ULONG r=DoMethod(m_hamb,MUIM_Menustrip_Popup,(ULONG)b,0,x,y);   /* MUI runs the menu here and returns when it closes */
  g_wd_s=0;   /* (the time the menu was open is not a stalled main loop) */
  m_hamb_old=m_hamb; m_hamb=NULL;   /* built fresh (zoom, sound...) each time; disposed of next time or at quit */
  asb_log("menu 1 (MUI popup) closed (%lu)",(unsigned long)r); STKLOG("menu");
  g_hamb_pick=ud_rid(r);   /* MUI returns the chosen item's MUIA_UserData (0: none); run next pass */
  return 1; }
/* bar folder / \xBB popups: MUI 5's popup menu under the button, else our own menu in the page */
static Object *g_bmpop_anchor;
static void bmi_popup(Object *b){
  if(!b) return;
  if(MUIMasterBase->lib_Version<20){ pop_from_bmi(_mleft(b)-_mleft(m_page),0); return; }
  int n=g_nbmi; hamb_free(); pop_close();
  Object *menu=MenuObject, MUIA_Menu_Title,(ULONG)"Bookmarks", End; if(!menu) return;
  g_nbmi=n; bmi_fill(menu);
  m_hamb=MenustripObject, MUIA_Family_Child,(ULONG)menu, End;
  if(!m_hamb){ MUI_DisposeObject(menu); return; }
  CRUMB("bookmarks popup: %d rows",n);
  ULONG r=DoMethod(m_hamb,MUIM_Menustrip_Popup,(ULONG)b,0,(LONG)_mleft(b),(LONG)_mbottom(b)+1);   /* (x,y: window coordinates, as for the menu button) */
  g_wd_s=0; m_hamb_old=m_hamb; m_hamb=NULL;
  asb_log("bookmarks popup closed (%lu)",(unsigned long)r);
  g_hamb_pick=ud_rid(r); }
static int g_bmpop_req, g_bmpop_f;   /* 1 folder g_bmpop_f, 2 the rest */
static void bm_popup_poll(void){
  int q=g_bmpop_req; g_bmpop_req=0; if(!q) return;
  if(q==1) bmi_onefolder(g_bmpop_f); else bmi_rest(g_nshown,MUIMasterBase->lib_Version>=20);   /* (a popup opened from another popup's pick closes at once on MUI 5: submenus instead) */
  bmi_popup(g_bmpop_anchor); }
static void hamb_open(void){
  g_hamb_req=0;
  if(g_popkind==1 && g_popn){ pop_close(); return; }   /* second press closes our own menu */
  if(hamb_popup()) return;
  menu_main(g_W-2,0); }
static void handle_rid(ULONG id){
  if(id==RID_CLOSE){   /* MUI turns Esc into a close request too: only the close gadget quits */
    if(g_esc_us && asb_us()-g_esc_us<700000ULL){ g_esc_us=0; }
    else if(!g_ownscr) g_quit=1; }
  else if(id==RID_URL){ g_omni=0; pop_close(); char u[2400]; url_from_input(url_text(),u,sizeof u); send_nav(u); set(m_win,MUIA_Window_ActiveObject,(ULONG)m_page); }
  else if(id==RID_URLCHG){ if(url_editing()) g_sugg_req=1; }
  else if(id==RID_FIND) send_find();
  else if(id==RID_FINDPREV) send_cmd("findprev");
  else if(id==RID_FINDNEXT) send_cmd("findnext");
  else if(id==RID_FINDCLOSE) find_show(0);
  else if(id==RID_NEWTAB){ if(g_fd>=0) asbc_send(g_fd,ASBC_TABNEW,NULL,0); }
  else if(id==RID_SET_SAVE) settings_done(2);
  else if(id==RID_SET_USE) settings_done(1);
  else if(id==RID_SET_CANCEL) settings_done(0);
  else if(id>=RID_TAB0 && id<RID_TAB0+MAXTAB){ unsigned char ix=(unsigned char)(id-RID_TAB0);
    nnset(m_tab[ix],MUIA_Selected,ix==g_tab); if(ix!=g_tab && g_fd>=0) asbc_send(g_fd,ASBC_TABSEL,&ix,1); }
  else if(id>=RID_TABX0 && id<RID_TABX0+MAXTAB){ unsigned char ix=(unsigned char)(id-RID_TABX0); if(g_fd>=0) asbc_send(g_fd,ASBC_TABCLS,&ix,1); }
  else if(id>=RID_TB0 && id<RID_TB0+NTB){ int b=(int)(id-RID_TB0), ic=b==2&&g_loading?IC_STOP:tb_ic[b];
    if(ic==IC_BACK) do_menu(M_BACK,NULL); else if(ic==IC_FWD) do_menu(M_FWD,NULL);
    else if(ic==IC_RELOAD) do_menu(M_RELOAD,NULL); else if(ic==IC_STOP) do_menu(M_STOP,NULL);
    else if(ic==IC_HOME) go_home(); else if(ic==IC_STAR) bm_add();
    else if(ic==IC_BOOKMARKS) open_page("bookmarks"); else if(ic==IC_HISTORY) open_page("history");
    else if(ic==IC_FIND){ find_show(!g_find_on); MUI_Redraw(m_tb[7],MADF_DRAWOBJECT); }
    else if(ic==IC_MENU) g_hamb_req=1; }   /* 0.79: opened after MUI has finished with the button (main loop) */
  else if(id>=RID_BMK0 && id<RID_BMK0+BMMAX) bm_open((int)(id-RID_BMK0),qual_shift());
  else if(id>=RID_BMF0 && id<RID_BMF0+BMFMAX){ g_bmpop_req=1; g_bmpop_f=(int)(id-RID_BMF0); if(!g_bmpop_anchor) g_bmpop_anchor=m_bmmore; }
  else if(id>=RID_BMB0 && id<RID_BMB0+NBMB){ int k=(int)(id-RID_BMB0);
    if(!g_bm_known){ open_page("bookmarks"); return; }
    if(k<g_nshown){ int sl=g_slot[k]; if(sl>=0) bm_open(sl,qual_shift()); else { g_bmpop_req=1; g_bmpop_f=-1-sl; g_bmpop_anchor=m_bmb[k]; } } }
  else if(id==RID_BMMORE){ g_bmpop_req=2; g_bmpop_anchor=m_bmmore; }
  else if(id>=RID_MENU0 && id<RID_MENU0+M_NUM+2){ int m=(int)(id-RID_MENU0);
    if(m==M_NUM+1) DoMethod(m_app,MUIM_Application_OpenConfigWindow,0);
    else if(m==M_FIND){ find_show(1); MUI_Redraw(m_tb[7],MADF_DRAWOBJECT); }
    else do_menu(m,NULL); } }

#include "asb_rexx.inc"
int main(int argc, char **argv){
  stack_mark(); store_migrate(); prefs_load(); win_box_load();
  tooltypes_load(argc,argv);
  for(int i=1;i<argc;i++){
    if(!strcmp(argv[i],"-host")&&i+1<argc) snprintf(g_host,sizeof g_host,"%s",argv[++i]);
    else if(!strcmp(argv[i],"-port")&&i+1<argc) g_port=atoi(argv[++i]);
    else if(!strcmp(argv[i],"-token")&&i+1<argc) snprintf(g_token,sizeof g_token,"%s",argv[++i]);
    else if(strchr(argv[i],'=') && strncasecmp(argv[i],"http",4)){ char k[40]; const char *eq=strchr(argv[i],'='); int kl=(int)(eq-argv[i]); if(kl>39) kl=39; memcpy(k,argv[i],(size_t)kl); k[kl]=0; theme_tooltype(k,eq+1); }
    else if(!strcasecmp(argv[i],"MODEREQ")) theme_tooltype("MODEREQ",NULL);
    else if(!strcasecmp(argv[i],"STATS")) theme_tooltype("STATS",NULL);
    else if(argv[i][0]!='-'){ snprintf(g_url,sizeof g_url,"%s",argv[i]); g_url_given=1; }
  }
  if(rexx_start()) return 0;   /* 0.85: another AmiSurf runs and took the URL */
  g_tabh=g_toolh=0; g_stath=0;
  g_jbuf=malloc(g_jcap); if(!g_jbuf) return 20;
  IntuitionBase=(struct IntuitionBase*)OpenLibrary((STRPTR)"intuition.library",39);
  GfxBase=(struct GfxBase*)OpenLibrary((STRPTR)"graphics.library",39);
  if(!IntuitionBase||!GfxBase){ printf("needs OS 3.0+\n"); return 20; }
  MUIMasterBase=OpenLibrary((STRPTR)MUIMASTER_NAME,19);
  if(!MUIMasterBase){
    struct EasyStruct es={sizeof(struct EasyStruct),0,(UBYTE*)"AmiSurf",(UBYTE*)"AmiSurf needs MUI 3.8 or newer\n(muimaster.library V19).\n\nAmiSurf-gadtools runs without MUI.",(UBYTE*)"Quit"};
    EasyRequestArgs(NULL,&es,NULL,NULL); return 20; }
  KeymapBase=OpenLibrary((STRPTR)"keymap.library",37);
  P96Base=OpenLibrary((STRPTR)"Picasso96API.library",2);
  SocketBase=OpenLibrary((STRPTR)"bsdsocket.library",4);
  if(!SocketBase){ MUI_RequestA(NULL,NULL,0,(STRPTR)"AmiSurf",(STRPTR)"Quit",(STRPTR)"AmiSurf needs a TCP/IP stack\n(bsdsocket.library).",NULL); CloseLibrary(MUIMasterBase); return 20; }
  CyberGfxBase=OpenLibrary((STRPTR)"cybergraphics.library",40);
  if(screen_open()){ scr=LockPubScreen(NULL); if(!scr) return 20; g_ownscr=0; g_aga=0; }
  TR("theme_pens");
  theme_pens();
  m_pagecl=MUI_CreateCustomClass(NULL,(STRPTR)MUIC_Area,NULL,sizeof(struct pagedata),(APTR)page_dispatch);
  m_iconcl=MUI_CreateCustomClass(NULL,(STRPTR)MUIC_Area,NULL,sizeof(struct icondata),(APTR)icon_dispatch);
  if(!m_pagecl||!m_iconcl){ printf("MUI classes failed\n"); return 20; }
  TR("classes ok");
  icons_load();
  struct Window *splash=NULL; ULONG sp_s=0, sp_u=0;
  if(g_splash_ms>0){ char st[96]; target_pick(); snprintf(st,sizeof st,"Connecting to %s:%d ...",g_th,g_tp); splash=splash_open(st); CurrentTime(&sp_s,&sp_u); }

  TR("splash done");
  Object *tabs=HGroup, MUIA_Group_Spacing,2, End, *fp,*fn,*fx, *strip=build_menus();
  for(int i=0;i<MAXTAB;i++){
    m_tabg[i]=HGroup, MUIA_Group_Spacing,0, MUIA_ShowMe,i==0,
      Child,(ULONG)(m_tab[i]=TextObject, ButtonFrame, MUIA_Background,MUII_ButtonBack, MUIA_Font,MUIV_Font_Button,
        MUIA_InputMode,MUIV_InputMode_Toggle, MUIA_Text_PreParse,(ULONG)"\33c", MUIA_Text_SetMin,FALSE, MUIA_Text_Contents,(ULONG)"New Tab", MUIA_Selected,i==0,
        MUIA_ShortHelp,(ULONG)"Tab (Amiga 1-8)", End),
      Child,(ULONG)(m_tabx[i]=sbtn(" x ","Close tab (Amiga W)")), End;
    DoMethod(tabs,OM_ADDMEMBER,(ULONG)m_tabg[i]); }
  Object *plus=sbtn(" + ","New tab (Amiga T)");
  DoMethod(tabs,OM_ADDMEMBER,(ULONG)plus);
  DoMethod(tabs,OM_ADDMEMBER,(ULONG)(RectangleObject, MUIA_Weight,250, End));   /* keeps a single tab from filling the strip */
  TR("tabs ok");
  for(int i=0;i<NTB;i++) m_tb[i]=iconbtn(tb_ic[i],1,tb_help[i]);
  m_lock=iconbtn(IC_LOCK,0,"Secure (https) or not");
  TR("icons ok");
  bmbar_build();
  int wx=0, wy=0, ww=scr->Width, wh=scr->Height;
  if(!g_ownscr) win_box(scr,&wx,&wy,&ww,&wh);
  mui_env_note();   /* 0.86: which ENV:MUI/AMISURF* files are the user's (see asb_store.inc) */
  m_app=ApplicationObject, MUIA_Application_Title,(ULONG)"AmiSurf",
    MUIA_Application_Version,(ULONG)"$VER: AmiSurf " ASB_VERSION " (19.10.2026)",
    MUIA_Application_Copyright,(ULONG)"AmiStudio", MUIA_Application_Author,(ULONG)"AmiStudio",
    MUIA_Application_Base,(ULONG)"AMISURF", MUIA_Application_SingleTask,FALSE,
    MUIA_Application_Description,(ULONG)"Web browser (pages rendered by Chrome on the host)",
    SubWindow,(ULONG)(m_win=WindowObject, MUIA_Window_Title,(ULONG)(g_ownscr?NULL:"AmiSurf"),   /* a title would draw a bar even borderless */
      MUIA_Window_Screen,(ULONG)scr, MUIA_Window_Menustrip,(ULONG)strip,
      MUIA_Window_LeftEdge,wx, MUIA_Window_TopEdge,wy, MUIA_Window_Width,ww, MUIA_Window_Height,wh,
      g_ownscr?MUIA_Window_Backdrop:TAG_IGNORE,TRUE, g_ownscr?MUIA_Window_Borderless:TAG_IGNORE,TRUE,
      g_ownscr?MUIA_Window_DepthGadget:TAG_IGNORE,FALSE, g_ownscr?MUIA_Window_DragBar:TAG_IGNORE,FALSE,
      g_ownscr?MUIA_Window_CloseGadget:TAG_IGNORE,FALSE, g_ownscr?MUIA_Window_SizeGadget:TAG_IGNORE,FALSE,
      WindowContents,(ULONG)(VGroup, MUIA_Group_Spacing,2, MUIA_InnerLeft,0, MUIA_InnerRight,0, MUIA_InnerTop,0, MUIA_InnerBottom,0,
        Child,(ULONG)(m_top=VGroup, MUIA_Group_Spacing,2,
          Child,(ULONG)tabs,
          Child,(ULONG)(HGroup, MUIA_Group_Spacing,2,
            Child,(ULONG)m_tb[0], Child,(ULONG)m_tb[1], Child,(ULONG)m_tb[2], Child,(ULONG)m_tb[3],
            Child,(ULONG)m_lock,
            Child,(ULONG)(m_url=field(2400,"Address or search (Amiga L)",&g_urlhook,1)),
            Child,(ULONG)m_tb[4], Child,(ULONG)m_tb[5], Child,(ULONG)m_tb[6], Child,(ULONG)m_tb[7], Child,(ULONG)m_tb[8], End),
          Child,(ULONG)m_bmbar, End),
        Child,(ULONG)(m_page=NewObject(m_pagecl->mcc_Class,NULL, MUIA_FillArea,FALSE, MUIA_CycleChain,1, TAG_DONE)),
        Child,(ULONG)(m_findbar=HGroup, MUIA_ShowMe,FALSE,
          Child,(ULONG)Label2((STRPTR)"Find:"),
          Child,(ULONG)(m_find=field(180,"Find in page (Return: next)",&g_findhook,0)),
          Child,(ULONG)(m_findcnt=TextObject, MUIA_Text_PreParse,(ULONG)"\33r", MUIA_FixWidthTxt,(ULONG)"0000 of 0000", MUIA_Text_Contents,(ULONG)"", End),
          Child,(ULONG)(fp=sbtn(" < ","Previous match (Shift Amiga G)")), Child,(ULONG)(fn=sbtn(" > ","Next match (Amiga G)")), Child,(ULONG)(fx=sbtn(" x ","Close (Esc)")), End),
        Child,(ULONG)(m_statbar=HGroup, MUIA_Group_Spacing,2,
          Child,(ULONG)(m_status=TextObject, TextFrame, MUIA_Background,MUII_TextBack, MUIA_Text_SetMin,FALSE, MUIA_Weight,300, MUIA_Text_Contents,(ULONG)"", End),
          Child,(ULONG)(m_info=TextObject, TextFrame, MUIA_Background,MUII_TextBack, MUIA_Text_SetMin,FALSE, MUIA_Text_PreParse,(ULONG)"\33r", MUIA_Text_Contents,(ULONG)"", End), End),
        End), End),
    End;
  TR("app built");
  if(!m_app){ MUI_RequestA(NULL,NULL,0,(STRPTR)"AmiSurf",(STRPTR)"Quit",(STRPTR)"Could not create the MUI application.",NULL); return 20; }
  DoMethod(m_win,MUIM_Notify,MUIA_Window_CloseRequest,TRUE,(ULONG)m_app,2,MUIM_Application_ReturnID,RID_CLOSE);
  DoMethod(m_url,MUIM_Notify,MUIA_String_Acknowledge,MUIV_EveryTime,(ULONG)m_app,2,MUIM_Application_ReturnID,RID_URL);
  DoMethod(m_url,MUIM_Notify,MUIA_String_Contents,MUIV_EveryTime,(ULONG)m_app,2,MUIM_Application_ReturnID,RID_URLCHG);
  DoMethod(m_find,MUIM_Notify,MUIA_String_Acknowledge,MUIV_EveryTime,(ULONG)m_app,2,MUIM_Application_ReturnID,RID_FIND);
  DoMethod(fp,MUIM_Notify,MUIA_Pressed,FALSE,(ULONG)m_app,2,MUIM_Application_ReturnID,RID_FINDPREV);
  DoMethod(fn,MUIM_Notify,MUIA_Pressed,FALSE,(ULONG)m_app,2,MUIM_Application_ReturnID,RID_FINDNEXT);
  DoMethod(fx,MUIM_Notify,MUIA_Pressed,FALSE,(ULONG)m_app,2,MUIM_Application_ReturnID,RID_FINDCLOSE);
  DoMethod(plus,MUIM_Notify,MUIA_Pressed,FALSE,(ULONG)m_app,2,MUIM_Application_ReturnID,RID_NEWTAB);
  for(int i=0;i<MAXTAB;i++){
    DoMethod(m_tab[i],MUIM_Notify,MUIA_Selected,MUIV_EveryTime,(ULONG)m_app,2,MUIM_Application_ReturnID,RID_TAB0+i);
    DoMethod(m_tabx[i],MUIM_Notify,MUIA_Pressed,FALSE,(ULONG)m_app,2,MUIM_Application_ReturnID,RID_TABX0+i); }
  for(int i=0;i<NTB;i++) DoMethod(m_tb[i],MUIM_Notify,MUIA_Pressed,FALSE,(ULONG)m_app,2,MUIM_Application_ReturnID,RID_TB0+i);
  menus_notify(strip);
  if(g_stats) set(m_mstats,MUIA_Menuitem_Checked,TRUE);
  nnset(m_mbmbar,MUIA_Menuitem_Checked,g_bmbar);
  for(int i=0;i<NBMB;i++) DoMethod(m_bmb[i],MUIM_Notify,MUIA_Pressed,FALSE,(ULONG)m_app,2,MUIM_Application_ReturnID,RID_BMB0+i);
  DoMethod(m_bmmore,MUIM_Notify,MUIA_Pressed,FALSE,(ULONG)m_app,2,MUIM_Application_ReturnID,RID_BMMORE);
  nnset(m_url,MUIA_String_Contents,(ULONG)g_url);
  snprintf(g_status,sizeof g_status,"AmiSurf %s",ASB_VERSION);
  TR("notifies ok");
  set(m_win,MUIA_Window_Open,TRUE);
  if(!xget(m_win,MUIA_Window_Open)){ MUI_RequestA(m_app,NULL,0,(STRPTR)"AmiSurf",(STRPTR)"Quit",(STRPTR)"Could not open the window.",NULL); MUI_DisposeObject(m_app); mui_env_tidy(); return 20; }
  TR("window open");
  win=(struct Window*)xget(m_win,MUIA_Window_Window);
  if(splash) WindowToFront(splash);   /* 0.81: the main window opened over it */
  if(g_ownscr) ShowTitle(scr,FALSE);   /* screen bar behind our backdrop window */
  detect_rtg();
  for(int i=0;i<NTB;i++) MUI_Redraw(m_tb[i],MADF_DRAWOBJECT);   /* icons were drawn as letters before RTG was known */
  MUI_Redraw(m_lock,MADF_DRAWOBJECT);
#ifdef ASB_TRACE
  { char t[120]; snprintf(t,sizeof t,"win flags %08lx at %d,%d %dx%d scr %dx%d",(unsigned long)win->Flags,win->LeftEdge,win->TopEdge,win->Width,win->Height,scr->Width,scr->Height); TR(t); }
#endif
  theme_font(); if(RP && g_font) SetFont(RP,g_font);
  set(m_win,MUIA_Window_ActiveObject,(ULONG)m_page);
  TR("before connect");
  log_startup(); if(connect_host()!=0) autohost();
  if(splash){ ULONG s2,u2; for(;;){ CurrentTime(&s2,&u2); long ms=(long)(s2-sp_s)*1000+((long)u2-(long)sp_u)/1000; if(ms>=g_splash_ms) break; Delay(2); } splash_close(splash); }
  TR("connected");
  draw();

  ULONG sigs=0; int run=1;
  while(run){
    loop_watch(); WHERE("mui input");
    ULONG id=DoMethod(m_app,MUIM_Application_NewInput,(ULONG)&sigs);
    if(id==(ULONG)MUIV_Application_ReturnID_Quit) break;
    if(id){ WHERE("menu/button action"); handle_rid(id); }
    else if(g_hamb_pick){ ULONG p=g_hamb_pick; g_hamb_pick=0; WHERE("menu action"); CRUMB("menu action %lu",(unsigned long)(p-RID_MENU0)); handle_rid(p); g_wd_s=0; }   /* (a requester it opened is not a stall) */
    if(g_hamb_req){ WHERE("hamburger menu"); hamb_open(); }
    if(g_bmpop_req){ WHERE("bookmarks popup"); bm_popup_poll(); }
    if(g_bm_dirty){ g_bm_dirty=0; WHERE("bookmarks bar"); mui_bm_relayout(); }
#ifdef ASB_HB
    { static ULONG ls; static long lc; ULONG s2,u2; CurrentTime(&s2,&u2); lc++; if(s2!=ls){ ls=s2; asb_log("hb %ld id %lu sigs %08lx",lc,(unsigned long)id,(unsigned long)sigs); lc=0; } if(id) asb_log("rid %lu",(unsigned long)id); }
#endif
    WHERE("audio"); stats_tick(); audio_service(); WHERE("menu window"); pop_win_poll(); menu_state_poll();
    WHERE("fields"); field_poll();
    WHERE("arexx"); rexx_poll();
    ev_tail();
    if(g_quit) break;
    if(g_fd>=0){
      fd_set rf; FD_ZERO(&rf); FD_SET(g_fd,&rf);
      struct timeval tv; tv.tv_sec=0; tv.tv_usec=sigs?30000:0;   /* sigs==0: MUI has more input queued, do not sleep */
      ULONG z=sigs|SIGBREAKF_CTRL_C|audio_sigmask()|pop_win_sig()|rexx_sigmask();
      if(g_popwin) tv.tv_usec=0;   /* menu window open: poll (UAE's WaitSelect does not wake for its signal) */
      WHERE("wait"); int ns=WaitSelect(g_fd+1,&rf,NULL,NULL,&tv,&z);
      if(g_popwin && ns<=0) Delay(1);
      if(z&SIGBREAKF_CTRL_C) break;
      if(ns>0 && g_fd>=0 && FD_ISSET(g_fd,&rf)){ WHERE("receiving from host"); pump(); }
    } else {
      /* offline: UAE's WaitSelect() with no sockets ignores the timeout, so poll (reconnect every ~2 s) */
      if(SetSignal(0,0)&sigs){ } else Delay(3);
      if(SetSignal(0,SIGBREAKF_CTRL_C)&SIGBREAKF_CTRL_C) break;
      reconnect_poll();
    }
  }
  rc_abort(); if(g_fd>=0) CloseSocket(g_fd);
  pop_win_close(); STKLOG("quit"); asb_log("quit"); audio_close(); rexx_end(); qual_close();
  win_box_save((struct Window*)xget(m_win,MUIA_Window_Window));
  set(m_win,MUIA_Window_Open,FALSE);
  hamb_free(); MUI_DisposeObject(m_app); mui_env_tidy(); bm_free(); MUI_DeleteCustomClass(m_pagecl); MUI_DeleteCustomClass(m_iconcl);
  theme_pens_free(); icons_free(); free(g_chunky); free(g_scaled); free(g_rgb); free(g_nat); free(g_jbuf);
  if(g_font) CloseFont(g_font);
  if(g_ownscr) CloseScreen(scr); else UnlockPubScreen(NULL,scr);
  if(AslBase) CloseLibrary(AslBase);
  if(DiskfontBase) CloseLibrary(DiskfontBase);
  if(g_tmr) CloseDevice((struct IORequest*)&g_tr);
  if(KeymapBase) CloseLibrary(KeymapBase);
  if(P96Base) CloseLibrary(P96Base);
  if(CyberGfxBase) CloseLibrary(CyberGfxBase);
  CloseLibrary(SocketBase); CloseLibrary(MUIMasterBase);
  CloseLibrary((struct Library*)GfxBase); CloseLibrary((struct Library*)IntuitionBase);
  return 0;
}
