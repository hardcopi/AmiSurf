/* ---- 0.82: the AmiStudio Host (ASH v1, amistudio-host docs/PROTOCOL.md) ----
   AmiSurf connects to the Host on TCP 9220 and asks for the service "browser" (SERVICE=) at our ASBC version with
   the pairing token; after the Host's OK the socket carries plain ASBC (the Host already did the backend's ASBC
   handshake, so no second one). The Host starts and supervises asb-chromed: no AUTOHOST / own token logic here.
   Where the Host is: HOST= (tooltype, Settings, -host/-port), else the identity folder's Host file ("host[:port]",
   0.86: <ident>/Host, see asb_store.inc: AmiStudio/Prefs/ or PROGDIR:Data/), else 127.0.0.1:9220, else a UDP "ASH?"
   broadcast to port 9220 (answer "ASH!" + key=value lines).
   Token: TOKEN= if set, else <ident>/HostToken (shared with the AmiStudio launcher when AmiSurf is installed by it);
   without one a "Pair with Host" requester asks for the code from `amistudio-hostd pair`. Loopback needs no token.
   HOSTMODE=LEGACY: the 0.81 way (plain ASBC to asb-chromed on 9223, AUTOHOST for the AmiKit edition). */
#define ASH_PORT 9220
#define ASH_OK_ 0
static char g_dh[64]; static int g_dp;          /* Host found by the LAN search (this session) */
static int g_disc_due, g_us=-1; static ULONG g_ut0;
static char g_ash_msg[200]; static unsigned g_ash_ver;
static int g_note, g_note_shown, g_nohost_shown;  /* pending / last shown requester (hs code) */
static char g_note_where[80];
static char g_ttok[ASBC_MAX_TOKEN+1];             /* token sent in this attempt */

static void split_hp(const char *in,char *h,int hn,int *port){
  char t[96]; scopy_u(t,sizeof t,in); char *c=strrchr(t,':'); if(c&&atoi(c+1)>0){ *port=atoi(c+1); *c=0; } scopy_u(h,hn,t); }
/* where to connect this attempt (g_th:g_tp); g_tsrc 0 configured, 1 <ident>/Host, 2 automatic 127.0.0.1, 3 LAN search */
static void target_pick(void){
  static int once; if(!once){ once=1;   /* AmiKit edition (0.83): Host/run-host.sh starts the bundled AmiStudio Host (AUTOHOST) */
    { BPTR l=Lock((STRPTR)"PROGDIR:Host/run-host.sh",ACCESS_READ); if(l){ UnLock(l); asb_log("host: AmiKit edition (Host/ in the drawer): %s",g_legacy?"run-host.sh --legacy starts asb-chromed":"run-host.sh starts the bundled AmiStudio Host"); } }
    asb_log("host: %s",g_legacy?"legacy mode (plain ASBC to asb-chromed)":"AmiStudio Host mode (ASH, service browser)"); }
  int port=0; char h[64]="";
  if(g_host[0]) split_hp(g_host,h,sizeof h,&port);
  if(g_port>0) port=g_port;
  if(g_legacy){ scopy_u(g_th,sizeof g_th,h[0]?h:"127.0.0.1"); g_tp=port>0?port:ASBC_PORT; g_tsrc=h[0]?0:2; return; }
  if(port==ASBC_PORT){ static int once; if(!once){ once=1; asb_log("host: port 9223 is the old engine's; the AmiStudio Host is on 9220 (HOSTMODE=LEGACY keeps 9223)"); }
    port=0; if(!strcmp(h,"127.0.0.1")||!strcasecmp(h,"localhost")) h[0]=0; }   /* 0.81 prefs saved 127.0.0.1:9223 as the default */
  if(h[0]){ scopy_u(g_th,sizeof g_th,h); g_tp=port>0?port:ASH_PORT; g_tsrc=0; return; }
  char e[96]; if(ident_read("Host",e,sizeof e)){ int p2=ASH_PORT; split_hp(e,g_th,sizeof g_th,&p2); g_tp=p2; g_tsrc=1; return; }
  if(g_dh[0]){ scopy_u(g_th,sizeof g_th,g_dh); g_tp=g_dp; g_tsrc=3; return; }
  scopy_u(g_th,sizeof g_th,"127.0.0.1"); g_tp=ASH_PORT; g_tsrc=2; }
static void token_pick(void){
  if(g_legacy||g_token[0]){ scopy_u(g_ttok,sizeof g_ttok,g_token); return; }
  char e[200]; int k=0; g_ttok[0]=0;
  if(ident_read("HostToken",e,sizeof e))
    for(char *q=e;*q&&k<ASBC_MAX_TOKEN;q++) if((*q>='0'&&*q<='9')||(*q>='a'&&*q<='f')) g_ttok[k++]=*q;
  g_ttok[k]=0; }
static void put16le(unsigned char *p,unsigned v){ p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8); }
static int put_str(unsigned char *p,const char *s,int max){ int l=(int)strlen(s); if(l>max) l=max; p[0]=(unsigned char)l; memcpy(p+1,s,(size_t)l); return 1+l; }
static void client_id(char *b,int n){ UWORD af=SysBase->AttnFlags;
  snprintf(b,n,"AmiSurf/%s %s",ASB_VERSION,(af&0x80)?"68060":(af&AFF_68040)?"68040":(af&AFF_68030)?"68030":"68020"); }
static int ash_req(unsigned char *b,const char *svc,unsigned ver,const char *tok){
  char cid[48]; client_id(cid,sizeof cid);
  int o=0; memcpy(b,"ASH1",4); put16le(b+4,1); o=6;
  o+=put_str(b+o,svc,32); put16le(b+o,ver); o+=2; o+=put_str(b+o,tok,64); o+=put_str(b+o,cid,64); return o; }
/* the hello for this attempt (ASH or plain ASBC) */
static int hs_build(unsigned char *b){
  token_pick();
  if(!g_legacy) return ash_req(b,g_service,ASBC_VERSION,g_ttok);
  size_t n=strlen(g_ttok); if(n>ASBC_MAX_TOKEN) n=ASBC_MAX_TOKEN;
  memcpy(b,ASBC_MAGIC,4); asbc_put_u16(b+4,ASBC_VERSION); b[6]=(unsigned char)n; if(n) memcpy(b+7,g_ttok,n); return (int)(7+n); }
/* bytes of the reply needed so far (have = bytes read) */
static int hs_need(const unsigned char *b,int have){ if(g_legacy) return 7; return have<10?10:10+b[9]; }
/* 0 ok, -1 not the expected protocol, -2 ASBC token rejected, -(10+status) ASH status */
static int hs_parse(const unsigned char *b,int n){
  if(g_legacy) return (memcmp(b,ASBC_MAGIC,4)||asbc_get_u16(b+4)!=ASBC_VERSION)?-1:b[6]?-2:0;
  if(n<10||memcmp(b,"ASH1",4)) return -1;
  g_ash_ver=b[7]|(unsigned)b[8]<<8; int l=b[9]; if(l>n-10) l=n-10; if(l>(int)sizeof g_ash_msg-1) l=sizeof g_ash_msg-1;
  memcpy(g_ash_msg,b+10,(size_t)l); g_ash_msg[l]=0;
  return b[6]==ASH_OK_?0:-(10+b[6]); }
static const char *hs_why(int hs){
  static char w[300]; const char *m=g_ash_msg[0]?g_ash_msg:NULL;
  switch(hs){
  case -1: return g_legacy?"ASBC handshake failed (old asb-chromed?)":"is not an AmiStudio Host (an old asb-chromed? then set HOSTMODE=LEGACY)";
  case -2: return "asb-chromed rejected the TOKEN";
  case -3: return g_legacy?"answers but is not the browser engine (check the port)":"answers but not as an AmiStudio Host (check the port)";
  case -11: snprintf(w,sizeof w,"this Amiga is not paired with the AmiStudio Host%s%s",m?": ":"",m?m:""); return w;
  case -12: snprintf(w,sizeof w,"the AmiStudio Host has no '%s' service (add AmiSurf's engine to host.conf)",g_service); return w;
  case -13: snprintf(w,sizeof w,"AmiSurf is too old for this Host (it needs protocol %u or newer): update AmiSurf",g_ash_ver); return w;
  case -14: snprintf(w,sizeof w,"the AmiStudio Host is older than AmiSurf (protocol up to %u): update the Host",g_ash_ver); return w;
  case -15: snprintf(w,sizeof w,"the Host's browser is busy (another Amiga is using it)%s%s",m?": ":"",m?m:""); return w;
  case -16: snprintf(w,sizeof w,"refused by the AmiStudio Host%s%s",m?": ":"",m?m:" (too many failed tries: wait 5 minutes)"); return w;
  } if(hs<-10){ snprintf(w,sizeof w,"AmiStudio Host status %d%s%s",-hs-10,m?": ":"",m?m:""); return w; }
  return "handshake failed"; }
/* a handshake answer that the user must act on: show it once (from the main loop, ash_notice_poll) */
static void ash_note(int hs){
  if(g_legacy || (hs>-11 && hs!=-1)) return;
  g_note=hs; snprintf(g_note_where,sizeof g_note_where,"%s:%d",g_th,g_tp); }

/* ---- LAN search: "ASH?" to UDP 255.255.255.255:9220, first "ASH!" answer wins (polled, 1.5 s) ---- */
static void disc_stop(void){ if(g_us>=0){ CloseSocket(g_us); g_us=-1; } }
static int disc_start(void){
  disc_stop(); g_us=socket(AF_INET,SOCK_DGRAM,0); if(g_us<0) return -1;
  LONG one=1; setsockopt(g_us,SOL_SOCKET,SO_BROADCAST,&one,sizeof one);
  struct sockaddr_in a; memset(&a,0,sizeof a); a.sin_family=AF_INET; a.sin_port=htons(ASH_PORT); a.sin_addr.s_addr=0xFFFFFFFFUL;
  if(sendto(g_us,(APTR)"ASH?",4,0,(struct sockaddr*)&a,sizeof a)<0){ disc_stop(); return -1; }
  g_ut0=ms_now(); asb_log("host: searching the LAN for an AmiStudio Host (UDP %d)",ASH_PORT); return 0; }
/* 1 found (g_dh/g_dp), 0 still waiting, -1 nothing */
static int disc_poll(void){
  if(g_us<0) return -1;
  while(wait_sock(g_us,0,0)){
    char b[600]; struct sockaddr_in f; LONG fl=sizeof f; long k=recvfrom(g_us,(APTR)b,sizeof b-1,0,(struct sockaddr*)&f,&fl);
    if(k<4) break; b[k]=0; if(memcmp(b,"ASH!",4)) continue;
    int port=ASH_PORT; char nm[40]="?"; for(char *l=strtok(b+4,"\n");l;l=strtok(NULL,"\n")){ if(!strncmp(l,"port=",5)&&atoi(l+5)>0) port=atoi(l+5); else if(!strncmp(l,"name=",5)) scopy_u(nm,sizeof nm,l+5); }
    ULONG ip=ntohl(f.sin_addr.s_addr);
    snprintf(g_dh,sizeof g_dh,"%u.%u.%u.%u",(unsigned)((ip>>24)&255),(unsigned)((ip>>16)&255),(unsigned)((ip>>8)&255),(unsigned)(ip&255)); g_dp=port;
    asb_log("host: found AmiStudio Host '%s' at %s:%d",nm,g_dh,g_dp); disc_stop(); return 1; }
  if((long)(ms_now()-g_ut0)>1500){ disc_stop(); asb_log("host: no AmiStudio Host answered on the LAN"); return -1; }
  return 0; }

/* ---- pairing: service "pair" v1, frame PAIR_REQ "code\nname" -> PAIR_OK 64 hex | PAIR_FAIL message ---- */
static int rd_until(int s,unsigned char *b,int n,ULONG t0,long ms){
  int o=0; while(o<n){ long left=ms-(long)(ms_now()-t0); if(left<=0) return -3;
    if(!wait_sock(s,0,left>250?250:left)) continue; long k=recv(s,b+o,n-o,0); if(k<=0) return -1; o+=(int)k; } return 0; }
/* 0.86: the token goes into the directory ClientId came from (asb_store.inc); 1 = written */
static int token_save(const char *tok){ return ident_write("HostToken",tok); }
static const char *tok_where(void){ static char w[64]; snprintf(w,sizeof w,"%sHostToken",ident_dir()); return w; }
/* 0.83: a stable id for this Amiga, sent as PAIR_REQ line 3 so a Host 0.1.1+ replaces this Amiga's earlier token when
   it pairs again (also after an AmiSurf update changes the name line). 0.86: <ident>/ClientId (asb_store.inc). */
static void amiga_cid(char *out,int n){
  char e[80]; out[0]=0;
  if(ident_read("ClientId",e,sizeof e)){
    int k=0; for(char *q=e;*q&&k<64&&k<n-1;q++) if(*q>32&&*q<127) out[k++]=*q; out[k]=0; if(k>=8) return; }
  cid_make(out,n);   /* an unusable ClientId file: replace it where it is */
  if(ident_write("ClientId",out)) asb_log("host: new client id %s (%sClientId)",out,ident_dir()); }
static int ash_pair(const char *code,char *msg,int mn){
  struct sockaddr_in a; msg[0]=0;
  if(resolve_host(&a)){ snprintf(msg,mn,"%s",g_status); return -1; }
  int s=socket(AF_INET,SOCK_STREAM,0); if(s<0){ snprintf(msg,mn,"no socket"); return -1; }
  int timed=0; if(connect_timed(s,&a,CONNECT_MS,&timed)<0){ CloseSocket(s); snprintf(msg,mn,"%s %s:%d",timed?"no answer from":"cannot connect to",g_th,g_tp); return -1; }
  unsigned char b[400]; int n=ash_req(b,"pair",1,"");
  if(send(s,b,n,0)!=n){ CloseSocket(s); snprintf(msg,mn,"send failed"); return -1; }
  ULONG t0=ms_now();
  if(rd_until(s,b,10,t0,5000)||memcmp(b,"ASH1",4)||(b[9]&&rd_until(s,b+10,b[9],t0,5000))){ CloseSocket(s); snprintf(msg,mn,"%s:%d is not an AmiStudio Host",g_th,g_tp); return -1; }
  if(b[6]){ int hs=hs_parse(b,10+b[9]); CloseSocket(s); snprintf(msg,mn,"%s",hs_why(hs)); return -1; }
  char req[200], cid[48], aid[72]; client_id(cid,sizeof cid); amiga_cid(aid,sizeof aid); int rl=snprintf(req,sizeof req,"%s\n%s\n%s",code,cid,aid);
  unsigned long t=(unsigned long)rl+1; b[0]=(unsigned char)t; b[1]=(unsigned char)(t>>8); b[2]=(unsigned char)(t>>16); b[3]=(unsigned char)(t>>24); b[4]=1;
  memcpy(b+5,req,(size_t)rl);
  if(send(s,b,5+rl,0)!=5+rl){ CloseSocket(s); snprintf(msg,mn,"send failed"); return -1; }
  if(rd_until(s,b,5,t0,8000)){ CloseSocket(s); snprintf(msg,mn,"no answer from the Host"); return -1; }
  unsigned long fl=b[0]|(unsigned long)b[1]<<8|(unsigned long)b[2]<<16|(unsigned long)b[3]<<24; int op=b[4];
  if(fl<1||fl>300){ CloseSocket(s); snprintf(msg,mn,"bad answer from the Host"); return -1; }
  int pl=(int)fl-1; if(pl && rd_until(s,b,pl,t0,8000)){ CloseSocket(s); snprintf(msg,mn,"no answer from the Host"); return -1; }
  CloseSocket(s); b[pl]=0;
  if(op==2 && pl==64){ if(!token_save((char*)b)){ snprintf(msg,mn,"paired, but could not write %s",tok_where()); return -1; }
    asb_log("host: paired with %s:%d, token saved in %s",g_th,g_tp,tok_where()); return 0; }
  snprintf(msg,mn,"%s",pl?(char*)b:"pairing failed"); return -1; }

static LONG ask(const char *title,const char *gads,const char *body){
  struct EasyStruct es={sizeof(struct EasyStruct),0,(UBYTE*)title,(UBYTE*)"%s",(UBYTE*)gads};
  return EasyRequest(win,&es,NULL,(ULONG)body); }
/* the code from `amistudio-hostd pair` (GadTools, works under MUI too); 1 = entered */
static int pair_code_ask(char *code,int cn){
  if(!GadToolsBase) GadToolsBase=OpenLibrary((STRPTR)"gadtools.library",39);
  struct Screen *ps=scr?scr:NULL; if(!GadToolsBase||!ps) return 0;
  APTR vi=GetVisualInfo(ps,TAG_END); if(!vi) return 0;
  struct Gadget *gl=NULL,*g=CreateContext(&gl),*gs=NULL; struct NewGadget ng; memset(&ng,0,sizeof ng);
  struct TextAttr *ta=ps->Font; int fh=ta?ta->ta_YSize:8; int bl=ps->WBorLeft+8, bt=ps->WBorTop+fh+1+6;
  char l1[120]; snprintf(l1,sizeof l1,"Pair this Amiga with the AmiStudio Host at %s",g_th);
  const char *tx[2]={l1,"On the PC run:  amistudio-hostd pair   and type the 6-digit code."};
  int tw=fh*30; for(int i=0;i<2;i++){ int t=(int)TextLength(&ps->RastPort,(STRPTR)tx[i],strlen(tx[i])); if(t>tw) tw=t; }
  ng.ng_VisualInfo=vi; ng.ng_TextAttr=ta;
  ng.ng_LeftEdge=bl+fh*8; ng.ng_TopEdge=bt+3*(fh+4); ng.ng_Width=fh*10; ng.ng_Height=fh+6; ng.ng_GadgetText=(STRPTR)"Code"; ng.ng_Flags=PLACETEXT_LEFT; ng.ng_GadgetID=1;
  gs=g=CreateGadget(STRING_KIND,g,&ng,GTST_MaxChars,12,TAG_END);
  ng.ng_Flags=PLACETEXT_IN; ng.ng_TopEdge+=fh+14; ng.ng_Width=fh*8; ng.ng_LeftEdge=bl; ng.ng_GadgetText=(STRPTR)"Pair"; ng.ng_GadgetID=2; g=CreateGadget(BUTTON_KIND,g,&ng,TAG_END);
  ng.ng_LeftEdge=bl+tw-fh*8; ng.ng_GadgetText=(STRPTR)"Cancel"; ng.ng_GadgetID=3; g=CreateGadget(BUTTON_KIND,g,&ng,TAG_END);
  if(!g){ FreeGadgets(gl); FreeVisualInfo(vi); return 0; }
  int ww=bl+tw+8, wh=ng.ng_TopEdge+ng.ng_Height+6+ps->WBorBottom;
  struct Window *w=OpenWindowTags(NULL,WA_Left,(ps->Width-ww)/2,WA_Top,(ps->Height-wh)/2,WA_Width,ww,WA_Height,wh,
      WA_Title,(ULONG)"AmiSurf: Pair with Host",WA_CustomScreen,(ULONG)ps,WA_Gadgets,(ULONG)gl,
      WA_DragBar,TRUE,WA_DepthGadget,TRUE,WA_CloseGadget,TRUE,WA_Activate,TRUE,WA_SimpleRefresh,TRUE,WA_RMBTrap,TRUE,
      WA_IDCMP,STRINGIDCMP|BUTTONIDCMP|IDCMP_CLOSEWINDOW|IDCMP_REFRESHWINDOW|IDCMP_VANILLAKEY,TAG_END);
  if(!w){ FreeGadgets(gl); FreeVisualInfo(vi); return 0; }
  GT_RefreshWindow(w,NULL);
  struct RastPort *rp=w->RPort; SetAPen(rp,1); SetDrMd(rp,JAM1);
  for(int i=0;i<2;i++){ Move(rp,bl,bt+i*(fh+4)+(ta?ta->ta_YSize-2:6)); Text(rp,(STRPTR)tx[i],strlen(tx[i])); }
  ActivateGadget(gs,w,NULL);
  int done=0, ok=0;
  while(!done){ WaitPort(w->UserPort); struct IntuiMessage *m;
    while((m=GT_GetIMsg(w->UserPort))){ ULONG cl=m->Class; UWORD code2=m->Code; struct Gadget *gg=(struct Gadget*)m->IAddress; GT_ReplyIMsg(m);
      if(cl==IDCMP_CLOSEWINDOW||(cl==IDCMP_VANILLAKEY&&code2==27)) done=1;
      else if(cl==IDCMP_REFRESHWINDOW){ GT_BeginRefresh(w); GT_EndRefresh(w,TRUE); }
      else if(cl==IDCMP_GADGETUP){ int id=gg->GadgetID; if(id==1||id==2){ done=ok=1; } else if(id==3) done=1; } } }
  char *src=(char*)((struct StringInfo*)gs->SpecialInfo)->Buffer; int k=0; for(char *q=src;*q&&k<cn-1;q++) if(*q>='0'&&*q<='9') code[k++]=*q; code[k]=0;
  CloseWindow(w); FreeGadgets(gl); FreeVisualInfo(vi);
  return ok&&k>0; }
static void reconnect_soon(void);
static void pair_flow(void){
  for(;;){
    char code[16], msg[240];
    WHERE("pair with Host dialog"); int ok=pair_code_ask(code,sizeof code); draw();
    if(!ok) return;
    set_status("Pairing with the AmiStudio Host ..."); CRUMB("pair %s:%d",g_th,g_tp);
    if(ash_pair(code,msg,sizeof msg)==0){ reconnect_soon(); { char m[200]; snprintf(m,sizeof m,g_token[0]?"Paired (token in %s) - but TOKEN= in Settings overrides it: clear that field":"Paired with the AmiStudio Host (token in %s) - connecting ...",tok_where()); set_status(m); } return; }
    asb_log("host: pairing failed: %s",msg);
    char b[400]; snprintf(b,sizeof b,"Pairing failed:\n%s",msg);
    if(ask("AmiSurf","Try again|Cancel",b)!=1) return; } }
/* from the offline loop: show what the Host said, once per kind of answer */
static void ash_notice_poll(void){
  if(!g_note || g_note==g_note_shown) return;
  int hs=g_note; g_note=0; g_note_shown=hs;
  char b[600]; CRUMB("host notice %d",hs);
  if(hs==-11){
    snprintf(b,sizeof b,"This Amiga is not paired with the AmiStudio Host\nat %s.%s%s\n\nPair now? You need the code that\n'amistudio-hostd pair' prints on the PC.",g_note_where,g_ash_msg[0]?"\n":"",g_ash_msg);
    if(ask("AmiSurf","Pair...|Later",b)==1) pair_flow(); return; }
  snprintf(b,sizeof b,"AmiStudio Host %s:\n%s%s",g_note_where,hs_why(hs),hs==-15||hs==-16?"\n\nAmiSurf keeps trying.":"");
  ask("AmiSurf","OK",b); }
static void nohost_notice(void){
  if(g_nohost_shown) return; g_nohost_shown=1;
  BPTR l=Lock((STRPTR)"PROGDIR:Host/run-host.sh",ACCESS_READ);
  if(l){ UnLock(l); ask("AmiSurf","OK","No AmiStudio Host found (127.0.0.1:9220).\n\nThis AmiKit edition brings its own Host in the\nHost drawer. AmiKit's Rabbit Hole (C:host-run) starts\nit; without it, on Linux run:  sh Host/run-host.sh\n(see Host/README-AMIKIT.txt and Host/data/host.log).\n\nAmiSurf keeps trying in the background."); return; }
  ask("AmiSurf","OK","No AmiStudio Host found\n(127.0.0.1:9220, the AmiStudio Host file, LAN search).\n\nAmiSurf needs the AmiStudio Host on a PC:\nstart amistudio-hostd there (see the ReadMe),\nor set HOST=<pc>[:port] in Settings or the icon.\nFor an old asb-chromed use HOSTMODE=LEGACY.\n\nAmiSurf keeps trying in the background."); }

/* ---- 0.83: pairing from the menus (≡ "AmiStudio Host (pair) ...", MUI menu Project: Pair with Host / Forget Host token) ---- */
static int token_have(char *where,int wn){
  char e[200]; if(ident_read("HostToken",e,sizeof e)){ scopy_u(where,wn,tok_where()); return 1; }
  return 0; }
static int legacy_says(void){
  if(!g_legacy) return 0;
  ask("AmiSurf","OK","HOSTMODE=LEGACY: AmiSurf talks to asb-chromed directly,\nwithout the AmiStudio Host, so there is nothing to pair.\n(Use TOKEN= for an asb-chromed started with -token.)");
  return 1; }
static void pair_now(void){
  CRUMB("menu: pair with Host"); if(legacy_says()) return;
  target_pick(); g_note=0; g_note_shown=-11;   /* no "not paired" requester on top of this one */
  pair_flow(); }
static void forget_token(int confirm){
  CRUMB("menu: forget Host token"); if(legacy_says()) return;
  char w[64]; if(!token_have(w,sizeof w)){ char m[160]; snprintf(m,sizeof m,"No AmiStudio Host token is stored on this Amiga\n(%s).",tok_where()); ask("AmiSurf","OK",m); return; }
  if(confirm && ask("AmiSurf","Forget|Cancel","Forget the AmiStudio Host token?\n\nThis Amiga then has to pair again to use a Host on\nthe network (an emulator on the Host PC needs none).\nThe AmiStudio launcher uses the same token.\nOn the PC, 'amistudio-hostd revoke ID' unpairs it there.")!=1) return;
  int n=ident_delete("HostToken");
  asb_log("host: token forgotten (%s%s)",tok_where(),n?" deleted":" could not be deleted");
  g_note_shown=0; reconnect_soon(); set_status("Host token forgotten - reconnecting ..."); }
static void host_menu(void){
  CRUMB("menu: AmiStudio Host"); if(legacy_says()) return;
  target_pick();
  char w[64]="", b[600]; int have=token_have(w,sizeof w);
  snprintf(b,sizeof b,"AmiStudio Host: %s:%d (%s)\n%s\nToken: %s%s",g_th,g_tp,
    g_tsrc==0?"HOST= / Settings":g_tsrc==1?"AmiStudio Host file":g_tsrc==3?"found on the LAN":"this PC",
    g_fd>=0?"connected":"not connected",g_token[0]?"TOKEN= (Settings/tooltype)":have?w:"none (pair to use a Host on the network)",
    g_token[0]?"\nNote: TOKEN= overrides the paired token; clear it in Settings.":"");
  LONG r=ask("AmiSurf",have?"Pair again...|Forget token|Cancel":"Pair...|Cancel",b);
  if(r==1) pair_now(); else if(r==2&&have) forget_token(0); }
