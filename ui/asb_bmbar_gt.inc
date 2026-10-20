/* ---- 0.87: the GadTools build's bookmarks bar: a drawn row under the toolbar (see asb_bmarks.inc) ---- */
static void gt_bm_height(void){ g_bmh=g_bmbar&&RP?RP->TxHeight+(g_large?12:8):0; if(g_bmh && can_rgb() && g_bmh<20) g_bmh=20; }   /* 0.88: room for the 16x16 icons */
#define GT_FAVW 20   /* 0.88: a 16x16 site icon left of each label (not on "\xBB") */
static int gt_bm_measure(const char *l){ return TextLength(RP,(STRPTR)l,(ULONG)strlen(l))+16+(can_rgb()&&strcmp(l,"\xBB")?GT_FAVW:0); }
static int gt_bm_y(void){ return TABH+TOOLH; }
static void gt_bm_draw(void){
  if(!g_bmh||g_full||!RP) return;
  int y=gt_bm_y(), h=g_bmh, ty=y+(h-RP->TxHeight)/2+RP->TxBaseline;
  fillp(0,y,g_W,h,tp[P_CHROME]); fillp(0,y+h-1,g_W,1,tp[P_SEP]);
  SetDrMd(RP,JAM1);
  if(!g_bm_known){ const char *t="Bookmarks"; SetAPen(RP,(ULONG)tp[P_TEXT]); Move(RP,OX+12,OY+ty); Text(RP,(STRPTR)t,(ULONG)strlen(t));
    const char *n="(the bookmarks bar needs the 0.87 engine)"; SetAPen(RP,(ULONG)tp[P_FAINT]); Move(RP,OX+12+gt_bm_measure(t)+8,OY+ty); Text(RP,(STRPTR)n,(ULONG)strlen(n)); return; }
  if(!g_nslot){ const char *n="No bookmarks yet: Amiga D or the star bookmarks this page"; SetAPen(RP,(ULONG)tp[P_FAINT]); Move(RP,OX+12,OY+ty); Text(RP,(STRPTR)n,(ULONG)strlen(n)); return; }
  char b[64];
  for(int k=0;k<g_nshown;k++){ int x=4+g_slotx[k]; slot_label(g_slot[k],b,sizeof b);
    int ix=0; if(can_rgb()){ fav_draw(fav_slot(k),x+6,y+(h-16)/2,P_CHROME); ix=GT_FAVW; }
    SetAPen(RP,(ULONG)tp[P_TEXT]); Move(RP,OX+x+8+ix,OY+ty); Text(RP,(STRPTR)b,(ULONG)strlen(b));
    fillp(x+g_slotw[k]+1,y+4,1,h-8,tp[P_SEP]); }
  if(g_morex>=0){ SetAPen(RP,(ULONG)tp[P_TEXT]); Move(RP,OX+4+g_morex+8,OY+ty); Text(RP,(STRPTR)"\xBB",1); } }
static void gt_bm_relayout(void){ if(!g_bmh||!RP) return; bm_layout(g_W-8,gt_bm_measure,2); if(!g_full) gt_bm_draw(); }
static void gt_bm_click(int mx,int newtab){
  int x=mx-4, y=gt_bm_y()+g_bmh;
  if(!g_bm_known){ open_page("bookmarks"); return; }
  if(g_morex>=0 && x>=g_morex){ menu_bm_rest(g_W-8,y); return; }
  for(int k=0;k<g_nshown;k++) if(x>=g_slotx[k] && x<g_slotx[k]+g_slotw[k]){ int s=g_slot[k];
    if(s>=0) bm_open(s,newtab); else menu_bm_folder(-1-s,4+g_slotx[k],y);
    return; } }
static void gt_bm_ctx(int mx){   /* 0.88: right-click on the bar */
  int x=mx-4, k=-1;
  if(g_bm_known) for(int i=0;i<g_nshown;i++) if(x>=g_slotx[i] && x<g_slotx[i]+g_slotw[i]) k=i;
  const char *lab[12]; int act[12]; int n=bm_ctx_rows(bm_slot_kind(k),lab,act,1);
  pop_close(); g_popkind=1;
  for(int i=0;i<n;i++){ char a[24]; snprintf(a,sizeof a,"%d %d",k,act[i]); pop_add(lab[i],act[i]?M_BMCTX:M_NONE,a); }
  asb_log("bookmarks bar menu at slot %d",k);
  pop_place(mx,gt_bm_y()+g_bmh,0); pop_draw(); }
static void bmbar_apply(void){
  gt_bm_height(); bm_layout(g_W-8,gt_bm_measure,2);
  pop_close(); send_hello(); fillp(0,0,g_W,g_H,tp[P_CANVAS]); draw(); }
