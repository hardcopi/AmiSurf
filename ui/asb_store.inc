/* ---- 0.86: everything AmiSurf keeps lives in its own drawer (AmiStudio suite rule "Self-contained install folder",
   /workspace/shared/amistudio/STYLE-additions.md). Nothing in ENV:, ENVARC:, S:, T: or assigns.
     PROGDIR:Data/AmiSurf.prefs    Settings (Save; "Use" only changes this run), Sound sync (AVSYNC=)
     PROGDIR:Data/AmiSurf.window   the window box (Workbench window mode)
     PROGDIR:AmiSurf.log (+ .old)  the log (asb_log.inc)
   One client per Amiga install (ClientId, HostToken, Host), ident_dir() is the single swap point. The first directory
   that has a ClientId file wins:
     1. PROGDIR://Prefs/  the AmiStudio launcher's (AmiSurf installed by it in AmiStudio/Apps/AmiSurf/)
     2. PROGDIR:/Prefs/   the AmiStudio launcher's (AmiSurf straight in the AmiStudio drawer)
     3. PROGDIR:Data/     AmiSurf's own (standalone); a ClientId is made there only if none exists anywhere
     4. once, read-only:  ENV:/ENVARC:AmiStudio/<key> (AmiStudio 0.1.7 and older, AmiSurf 0.83-0.85) seed step 3
   A new HostToken (Pair with Host) goes back into the directory ClientId came from.
   Moving from 0.85 and older (once, PROGDIR:Data/Migrated is the marker): AmiSurf.prefs / AmiSurf.window (and the
   AmiStudioBrowser.* ones of 0.81 and older) are copied from ENVARC:/ENV: into PROGDIR:Data/ and then deleted;
   the classic engine's ENVARC:AmiStudioBrowser.bookmarks/.cookies/.history (0.69 and older, not read since) go to
   PROGDIR:Data/Old/ and are deleted too;
   the shared ENV:/ENVARC:AmiStudio/ClientId, HostToken and Host are only copied (when needed) and the copy is read
   back; the originals and the AmiStudio drawers stay - the AmiStudio launcher 0.1.8 migrates and deletes them. ---- */
#define DATA_DIR "PROGDIR:Data"
#define MIGRATED_FILE "PROGDIR:Data/Migrated"
static int path_exists(const char *p){ BPTR l=Lock((STRPTR)p,ACCESS_READ); if(l) UnLock(l); return l!=0; }
static int data_dir(void){ BPTR l=Lock((STRPTR)DATA_DIR,ACCESS_READ); if(!l) l=CreateDir((STRPTR)DATA_DIR); if(l) UnLock(l); return l!=0; }
/* first line of a file, trimmed; 1 = non-empty */
static int env_line(const char *path,char *out,int n){
  FILE *f=fopen(path,"r"); out[0]=0; if(!f) return 0;
  if(!fgets(out,n,f)) out[0]=0; fclose(f); out[strcspn(out,"\r\n")]=0;
  char *p=out; while(*p==' '||*p=='\t') p++; if(p!=out) memmove(out,p,strlen(p)+1);
  int l=(int)strlen(out); while(l&&(out[l-1]==' '||out[l-1]=='\t')) out[--l]=0;
  return out[0]!=0; }
static int file_put(const char *path,const char *line){
  FILE *f=fopen(path,"w"); if(!f) return 0;
  int ok=fprintf(f,"%s\n",line)>0; if(fclose(f)) ok=0;
  char b[200]; return ok && env_line(path,b,sizeof b) && !strcmp(b,line); }   /* read back: only then is it safe to delete the old copy */
static int file_copy(const char *from,const char *to){
  FILE *a=fopen(from,"rb"); if(!a) return 0; FILE *b=fopen(to,"wb"); if(!b){ fclose(a); return 0; }
  char buf[1024]; size_t k; int ok=1;
  while((k=fread(buf,1,sizeof buf,a))>0) if(fwrite(buf,1,k,b)!=k){ ok=0; break; }
  fclose(a); if(fclose(b)) ok=0; if(!ok) DeleteFile((STRPTR)to); return ok; }

/* ---- identity ---- */
static const char *g_idir="PROGDIR:Data/";   /* ident_dir() result */
static int g_idir_src;                         /* 1..3 as above, 0 = not looked yet */
static int g_mig_due=-1;                       /* 1 = the one-time migration from ENV:/ENVARC: has not run yet */
static int mig_due(void){ if(g_mig_due<0) g_mig_due=!path_exists(MIGRATED_FILE); return g_mig_due; }
static int legacy_line(const char *key,char *out,int n){   /* read-only: ENV: copy first, as AmiSurf 0.85 did */
  char p[64]; snprintf(p,sizeof p,"ENV:AmiStudio/%s",key); if(env_line(p,out,n)) return 1;
  snprintf(p,sizeof p,"ENVARC:AmiStudio/%s",key); return env_line(p,out,n); }
static int cid_ok(const char *s){ int k=0; for(;*s;s++){ if(*s<=32||*s>=127) return 0; k++; } return k>=8&&k<=64; }
static void ident_path(char *b,int n,const char *key){ snprintf(b,n,"%s%s",g_idir,key); }
static void cid_make(char *out,int n){
  struct DateStamp ds; DateStamp(&ds); ULONG s1,u1; CurrentTime(&s1,&u1);
  ULONG a=(ULONG)ds.ds_Days*1440u+(ULONG)ds.ds_Minute, b=s1^(u1<<12)^(ULONG)FindTask(NULL)^(*(volatile UWORD*)0xDFF006<<16)^(ULONG)ds.ds_Tick;
  b^=(ULONG)AvailMem(MEMF_ANY)*2654435761u;
  snprintf(out,n,"amiga-%08x%08x",(unsigned)(a*2246822519u^b),(unsigned)(b*3266489917u+a)); }
/* the one-time look at ENV:/ENVARC:AmiStudio. fresh = no ClientId anywhere (step 3 is being set up now) */
static int g_ident_mig_ok=1;
static void ident_migrate(int fresh){
  char e[200], p[64]; int need_tok;
  ident_path(p,sizeof p,"HostToken"); need_tok=!path_exists(p);
  int have_cid=legacy_line("ClientId",e,sizeof e)&&cid_ok(e);
  if(fresh && have_cid){ ident_path(p,sizeof p,"ClientId");
    if(!file_put(p,e)){ asb_log("ident: could not write %s - ENV:AmiStudio/ClientId not copied",p); g_ident_mig_ok=0; return; }
    asb_log("ident: client id %s copied from ENV(ARC):AmiStudio/ClientId to %s (original left for the launcher)",e,p); }
  if(need_tok && legacy_line("HostToken",e,sizeof e)){ ident_path(p,sizeof p,"HostToken");
    if(!file_put(p,e)){ asb_log("ident: could not write %s - ENV:AmiStudio/HostToken not copied",p); g_ident_mig_ok=0; return; }
    asb_log("ident: Host token copied from ENV(ARC):AmiStudio/HostToken to %s (original left for the launcher)",p); }
  if(fresh && legacy_line("Host",e,sizeof e)){ ident_path(p,sizeof p,"Host");   /* the launcher's: copy only */
    if(file_put(p,e)) asb_log("ident: Host address %s copied from ENV(ARC):AmiStudio/Host to %s (original left for the launcher)",e,p); }
  /* the originals stay: they are shared with the AmiStudio launcher, which (0.1.8) migrates and deletes them */
}
/* where ClientId, HostToken and Host live for this run (see the top of this file) */
static const char *ident_dir(void){
  if(g_idir_src) return g_idir;
  static const char *dirs[3]={"PROGDIR://Prefs/","PROGDIR:/Prefs/","PROGDIR:Data/"};
  char p[64];
  for(int i=0;i<3;i++){ snprintf(p,sizeof p,"%sClientId",dirs[i]); if(path_exists(p)){ g_idir=dirs[i]; g_idir_src=i+1; break; } }
  int fresh=0;
  if(!g_idir_src){ g_idir=dirs[2]; g_idir_src=3; fresh=1; data_dir(); }
  asb_log("ident: %s%s",g_idir,g_idir_src==1?" (AmiStudio launcher, AmiSurf in Apps/)":g_idir_src==2?" (AmiStudio launcher, AmiSurf in its drawer)":fresh?" (own, new)":" (own)");
  if(mig_due()) ident_migrate(fresh);
  if(fresh){ char e[80]; snprintf(p,sizeof p,"%sClientId",g_idir);
    if(!env_line(p,e,sizeof e)){ cid_make(e,sizeof e); if(file_put(p,e)) asb_log("ident: new client id %s (%s)",e,p); else asb_log("ident: cannot write %s",p); } }
  return g_idir; }
static int ident_read(const char *key,char *out,int n){ char p[64]; ident_dir(); ident_path(p,sizeof p,key); return env_line(p,out,n); }
static int ident_write(const char *key,const char *val){ char p[64]; ident_dir(); if(g_idir_src==3) data_dir(); ident_path(p,sizeof p,key); return file_put(p,val); }
static int ident_delete(const char *key){ char p[64]; ident_dir(); ident_path(p,sizeof p,key); return DeleteFile((STRPTR)p)!=0; }

/* ---- own settings files: once, from ENVARC:/ENV: into PROGDIR:Data/ ---- */
#define PREF_FILE "PROGDIR:Data/AmiSurf.prefs"
#define WIN_FILE "PROGDIR:Data/AmiSurf.window"
static int move_old(const char *to,const char *const *from,int nf){
  int got=path_exists(to);
  for(int i=0;i<nf&&!got;i++) if(path_exists(from[i])){ got=file_copy(from[i],to); asb_log("settings: %s %s to %s",got?"moved":"could not copy",from[i],to); if(!got) return 0; }
  int n=0; for(int i=0;i<nf;i++) if(DeleteFile((STRPTR)from[i])) n++;
  if(n) asb_log("settings: deleted %d old file%s in ENV:/ENVARC:",n,n==1?"":"s");
  return 1; }
/* from main(), before prefs_load(): the 0.85-and-older files, then the marker; and the identity */
static void store_migrate(void){
  if(mig_due() && data_dir()){
    static const char *const pf[4]={"ENVARC:AmiSurf.prefs","ENV:AmiSurf.prefs","ENVARC:AmiStudioBrowser.prefs","ENV:AmiStudioBrowser.prefs"};
    static const char *const wf[4]={"ENV:AmiSurf.window","ENVARC:AmiSurf.window","ENV:AmiStudioBrowser.window","ENVARC:AmiStudioBrowser.window"};
    int ok=move_old(PREF_FILE,pf,4); ok&=move_old(WIN_FILE,wf,4);
    static const char *const cn[3]={"bookmarks","cookies","history"};   /* the classic engine's (0.69 and older) */
    for(int i=0;i<3;i++){ char a[48],b[48],to[64]; const char *fr[2]={a,b};
      snprintf(a,sizeof a,"ENVARC:AmiStudioBrowser.%s",cn[i]); snprintf(b,sizeof b,"ENV:AmiStudioBrowser.%s",cn[i]);
      if(!path_exists(a)&&!path_exists(b)) continue;
      { BPTR l=Lock((STRPTR)DATA_DIR "/Old",ACCESS_READ); if(!l) l=CreateDir((STRPTR)DATA_DIR "/Old"); if(l) UnLock(l); }
      snprintf(to,sizeof to,DATA_DIR "/Old/AmiStudioBrowser.%s",cn[i]); ok&=move_old(to,fr,2); }
    ident_dir(); ok&=g_ident_mig_ok;
    if(ok && file_put(MIGRATED_FILE,"AmiSurf " ASB_VERSION ": ENV:/ENVARC: settings and identity moved into the drawer")) g_mig_due=0;
  } else ident_dir(); }

/* ---- MUI 5 writes ENV:MUI/AMISURF*.prefs on every exit: delete what this run made (files that were there before are
   the user's own MUI settings for AmiSurf and stay; ENV:MUI last, only when empty) ---- */
static const char *const g_mui_env[]={"ENV:MUI/AMISURF.prefs","ENV:MUI/AMISURF.1.prefs","ENV:MUI/AMISURF.2.prefs","ENV:MUI/AMISURF.3.prefs","ENV:MUI",NULL};
static int g_mui_had[5];
static void __attribute__((unused)) mui_env_note(void){ for(int i=0;g_mui_env[i];i++) g_mui_had[i]=path_exists(g_mui_env[i]); }
static void __attribute__((unused)) mui_env_tidy(void){
  for(int i=0;g_mui_env[i];i++) if(!g_mui_had[i] && path_exists(g_mui_env[i]) && DeleteFile((STRPTR)g_mui_env[i])) asb_log("removed %s (MUI made it on quit)",g_mui_env[i]); }
