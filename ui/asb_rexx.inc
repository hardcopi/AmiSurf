/* AmiSurf 0.85: single instance and the ARexx port "AMISURF" (see docs/AREXX.md).
   Included by asb_gfx_chrome.c (GadTools and MUI clients). Commands:
     OPENURL "<url>" [NEWTAB]   open the URL in the current tab, or in a new tab (NEWTAB), and bring AmiSurf to the front
     FRONT                      bring the AmiSurf window (and its screen) to the front
     GETURL                     result: the current page's URL
   A second AmiSurf started while one runs sends OPENURL "<url>" NEWTAB (or FRONT without a URL) to this port and quits.
   The port is a plain public exec port; rexxsyslib.library builds and answers the messages, RexxMast is not needed for
   the hand-over (only for scripts started with rx). */
#include <proto/rexxsyslib.h>
#include <rexx/storage.h>
#include <rexx/errors.h>
#define RX_PORT "AMISURF"
struct RxsLib *RexxSysBase;
static struct MsgPort *g_rx;          /* our public port, NULL when another copy owns the name or there is no rexxsyslib */
static int g_url_given;               /* a URL came on the command line */
static int g_rx_front;                /* FRONT asked before the window was open */

static void rexx_end(void);
static void rx_skip(const char **p){ while(**p==' '||**p=='\t') (*p)++; }
/* next word or "quoted string" of *p into o; returns its length */
static int rx_word(const char **p,char *o,int cap){
  int n=0; rx_skip(p);
  if(**p=='"'||**p=='\''){ char q=*(*p)++; while(**p && **p!=q){ if(n<cap-1) o[n++]=**p; (*p)++; } if(**p) (*p)++; }
  else while(**p && **p!=' ' && **p!='\t' && **p!='\n'){ if(n<cap-1) o[n++]=**p; (*p)++; }
  o[n]=0; return n; }

/* Called right after the arguments are read. Returns 1 when a running AmiSurf took over (this copy should quit). */
static int rexx_start(void){
  RexxSysBase=(struct RxsLib*)OpenLibrary((STRPTR)"rexxsyslib.library",36);
  if(!RexxSysBase) return 0;
  struct MsgPort *reply=CreateMsgPort(); if(!reply) return 0;
  struct RexxMsg *rm=CreateRexxMsg(reply,NULL,NULL);
  char cmd[2500];
  if(g_url_given){ char u[2400]; int j=0; for(const char *s=g_url;*s && j<(int)sizeof u-1;s++) if(*s!='"') u[j++]=*s; u[j]=0;
    snprintf(cmd,sizeof cmd,"OPENURL \"%s\" NEWTAB",u); }
  else snprintf(cmd,sizeof cmd,"FRONT");
  if(rm){ rm->rm_Action=RXCOMM; rm->rm_Args[0]=(STRPTR)CreateArgstring((STRPTR)cmd,(ULONG)strlen(cmd)); }
  int sent=0;
  Forbid();
  struct MsgPort *other=FindPort((STRPTR)RX_PORT);
  if(other){ if(rm && rm->rm_Args[0]){ PutMsg(other,(struct Message*)rm); sent=1; } }
  else { g_rx=CreateMsgPort(); if(g_rx){ g_rx->mp_Node.ln_Name=(char*)RX_PORT; g_rx->mp_Node.ln_Pri=0; AddPort(g_rx); } }
  if(g_rx) atexit(rexx_end);   /* every exit path (early errors too) takes the port down: a port left behind would crash the next sender */
  Permit();
  if(!sent){ if(rm){ if(rm->rm_Args[0]) DeleteArgstring((UBYTE*)rm->rm_Args[0]); DeleteRexxMsg(rm); } DeleteMsgPort(reply); return other?0:0; }
  /* wait for the answer, at most ~10 s: a running copy that is stuck must not hang this one for good */
  struct Message *a=NULL;
  for(int i=0;i<200 && !(a=GetMsg(reply));i++) Delay(3);
  if(!a){ /* never free what the other copy may still answer: the reply port stays, PA_IGNORE so ReplyMsg only queues */
    reply->mp_Flags=PA_IGNORE; printf("AmiSurf is already running but does not answer (port " RX_PORT ")\n"); return 1; }
  DeleteArgstring((UBYTE*)rm->rm_Args[0]); DeleteRexxMsg(rm); DeleteMsgPort(reply);
  return 1; }

static ULONG rexx_sigmask(void){ return g_rx?(1UL<<g_rx->mp_SigBit):0; }

static void rexx_front(void){
#ifdef ASP_MUI
  if(m_app && xget(m_app,MUIA_Application_Iconified)) set(m_app,MUIA_Application_Iconified,FALSE);
  struct Window *w=m_win?(struct Window*)xget(m_win,MUIA_Window_Window):NULL;
#else
  struct Window *w=win;
#endif
  if(!w){ g_rx_front=1; return; }
  if(w->WScreen) ScreenToFront(w->WScreen);
  WindowToFront(w); ActivateWindow(w); }

static void rexx_poll(void){
  if(!g_rx) return;
  struct RexxMsg *rm;
  while((rm=(struct RexxMsg*)GetMsg(g_rx))){
    LONG rc=RC_OK; const char *res=NULL;
    /* not IsRexxMsg(): that only accepts messages from the interpreter, and a second AmiSurf sends its own */
    if(rm->rm_Node.mn_Length<sizeof(struct RexxMsg) || (rm->rm_Action&RXCODEMASK)!=RXCOMM || !rm->rm_Args[0]){ asb_log("arexx: not a command message"); rc=RC_FATAL; goto done; }
    { const char *p=(const char*)rm->rm_Args[0]; char w[24], u[2400], f[24];
      rx_word(&p,w,sizeof w);
      asb_log("arexx: %.200s",(const char*)rm->rm_Args[0]);
      if(!strcasecmp(w,"OPENURL")){
        if(!rx_word(&p,u,sizeof u)){ rc=RC_ERROR; goto done; }
        int nt=0; while(rx_word(&p,f,sizeof f)) if(!strcasecmp(f,"NEWTAB")) nt=1;
        char v[2400]; url_from_input(u,v,sizeof v);
        if(g_fd>=0){ if(nt) send_cmd("newtab %s",v); else send_nav(v); }
        else snprintf(g_url,sizeof g_url,"%s",v);   /* not connected yet: opened when the Host answers */
        rexx_front(); }
      else if(!strcasecmp(w,"FRONT")) rexx_front();
      else if(!strcasecmp(w,"GETURL")) res=g_url;
      else rc=RC_ERROR; }
  done:
    rm->rm_Result1=rc; rm->rm_Result2=0;
    if(rc==RC_OK && res && (rm->rm_Action&RXFF_RESULT)) rm->rm_Result2=(LONG)CreateArgstring((STRPTR)res,(ULONG)strlen(res));
    ReplyMsg((struct Message*)rm); }
  if(g_rx_front){ g_rx_front=0; rexx_front(); } }

static void rexx_end(void){
  if(g_rx){ Forbid(); RemPort(g_rx); struct Message *m; while((m=GetMsg(g_rx))){ ((struct RexxMsg*)m)->rm_Result1=RC_FATAL; ((struct RexxMsg*)m)->rm_Result2=0; ReplyMsg(m); } Permit(); DeleteMsgPort(g_rx); g_rx=NULL; }
  if(RexxSysBase){ CloseLibrary((struct Library*)RexxSysBase); RexxSysBase=NULL; } }
