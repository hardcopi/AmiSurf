/* ---- 0.77 client log: PROGDIR:AmiSurf.log (LOG=<file> tooltype, LOG=OFF to disable). Every line is
   appended and the file closed again, so the filesystem has it even if the machine hangs right after; the
   previous run's log is kept as <file>.old (so a reboot after a crash does not lose it). Lines starting with "@"
   are breadcrumbs written just before risky steps: after a crash, the last "@" line is where it died. ---- */
static char g_logpath[256]="PROGDIR:AmiSurf.log"; static int g_log=1, g_log_started;
static void asb_log(const char *fmt,...){
  if(!g_log) return;
  if(!g_log_started){ g_log_started=1; char old[270]; snprintf(old,sizeof old,"%s.old",g_logpath); DeleteFile((STRPTR)old); Rename((STRPTR)g_logpath,(STRPTR)old); }
  FILE *f=fopen(g_logpath,"a"); if(!f){ g_log=0; return; }
  ULONG s=0,u=0; if(IntuitionBase) CurrentTime(&s,&u);
  fprintf(f,"%02lu:%02lu:%02lu.%02lu ",(unsigned long)((s/3600)%24),(unsigned long)((s/60)%60),(unsigned long)(s%60),(unsigned long)(u/10000));
  va_list ap; va_start(ap,fmt); vfprintf(f,fmt,ap); va_end(ap);
  fputc('\n',f); fclose(f); }
#define CRUMB(...) asb_log("@ " __VA_ARGS__)
/* main-loop watchdog: a pass that took over 2 s is logged with the step it was in (g_where) */
static const char *g_where="start"; static ULONG g_wd_s, g_wd_u;
#define WHERE(s) (g_where=(s))
static void loop_watch(void){
  ULONG s,u; if(!IntuitionBase) return; CurrentTime(&s,&u);
  if(g_wd_s){ long ms=(long)(s-g_wd_s)*1000+((long)u-(long)g_wd_u)/1000; if(ms>2000) asb_log("main loop stalled %ld ms (in %s)",ms,g_where); }
  g_wd_s=s; g_wd_u=u; }
/* 0.87: stack size and high-water mark. The beta-reported Guru 8000 0004 after Settings... was a stack overflow: the
   icons carry no stack size, so Workbench starts AmiSurf with its small default stack, and __stack in
   asb_gfx_chrome.c was never honoured (libnix only swaps stacks when its __stkinit module is linked; it is now). stack_mark() fills the
   unused stack with a pattern once; stack_used() reports the deepest point reached since, logged at the
   breadcrumbs around menus and windows and at quit, so a future log shows how close it came. */
#define STK_PAT 0xA55AA55AUL
static ULONG g_stk_size;
static void stack_mark(void){
  struct Task *t=FindTask(NULL); volatile ULONG here=0;
  ULONG *lo=(ULONG*)(((ULONG)t->tc_SPLower+3)&~3UL), *hi=(ULONG*)(((ULONG)&here-1024)&~3UL);
  g_stk_size=(ULONG)t->tc_SPUpper-(ULONG)t->tc_SPLower;
  for(ULONG *p=lo; p<hi; p++) *p=STK_PAT; }
static ULONG stack_used(void){
  struct Task *t=FindTask(NULL); ULONG *p=(ULONG*)(((ULONG)t->tc_SPLower+3)&~3UL), *up=(ULONG*)t->tc_SPUpper;
  while(p<up && *p==STK_PAT) p++;
  return (ULONG)t->tc_SPUpper-(ULONG)p; }
/* logs the deepest point since the last STKLOG (or start), then marks again */
#define STKLOG(w) do{ asb_log("stack: %lu of %lu bytes used (%s)",(unsigned long)stack_used(),(unsigned long)g_stk_size,(w)); stack_mark(); }while(0)
