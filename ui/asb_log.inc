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
/* Stack size and high-water mark (0.87), own stack (0.88). stack_mark() fills the unused stack with a pattern once;
   stack_used() reports the deepest point reached since, logged around menus and windows and at quit.
   0.88: main() runs AmiSurf on a 256 KB stack of its own (StackSwap, below), so tc_SPLower/tc_SPUpper describe the
   stack we really run on. stack_mark() also refuses to fill anything when the stack pointer is not inside them. */
#define STK_PAT 0xA55AA55AUL
#define ASB_STACK 262144UL
static ULONG g_stk_size; static int g_stk_bad;
static ULONG g_stk_in_sp, g_stk_in_lo, g_stk_in_hi; static int g_stk_own;   /* how we were started (logged at startup) */
static void stack_mark(void){
  struct Task *t=FindTask(NULL); volatile ULONG here=0;
  ULONG *lo=(ULONG*)(((ULONG)t->tc_SPLower+3)&~3UL), *hi=(ULONG*)(((ULONG)&here-1024)&~3UL);
  g_stk_size=(ULONG)t->tc_SPUpper-(ULONG)t->tc_SPLower;
  g_stk_bad=(ULONG)&here<=(ULONG)t->tc_SPLower || (ULONG)&here>=(ULONG)t->tc_SPUpper;
  if(g_stk_bad) return;   /* never write outside the stack the task says it has */
  for(ULONG *p=lo; p<hi; p++) *p=STK_PAT; }
static ULONG stack_used(void){
  if(g_stk_bad) return 0;
  struct Task *t=FindTask(NULL); ULONG *p=(ULONG*)(((ULONG)t->tc_SPLower+3)&~3UL), *up=(ULONG*)t->tc_SPUpper;
  while(p<up && *p==STK_PAT) p++;
  return (ULONG)t->tc_SPUpper-(ULONG)p; }
/* logs the deepest point since the last STKLOG (or start), then marks again */
#define STKLOG(w) do{ if(g_stk_bad) asb_log("stack: pointer outside the task's stack (%s)",(w)); else asb_log("stack: %lu of %lu bytes used (%s)",(unsigned long)stack_used(),(unsigned long)g_stk_size,(w)); stack_mark(); }while(0)
static void stack_log_start(void){
  int in=g_stk_in_sp>g_stk_in_lo && g_stk_in_sp<g_stk_in_hi;
  asb_log("stack: %s; started with sp %08lx, task stack %08lx-%08lx (%lu bytes, sp %s)",g_stk_own?"own 256 KB stack":"NO own stack (out of memory)",
    (unsigned long)g_stk_in_sp,(unsigned long)g_stk_in_lo,(unsigned long)g_stk_in_hi,(unsigned long)(g_stk_in_hi-g_stk_in_lo),in?"inside":"OUTSIDE"); }
/* asb_on_stack(sss, fn, SysBase): StackSwap() to sss, call fn() there, StackSwap() back, return fn's result. In assembly
   so that nothing the compiler keeps on the stack is touched between the two swaps. */
__asm__(
"	.text\n"
"	.even\n"
"_asb_on_stack:\n"
"	movem.l d2-d7/a2-a6,-(sp)\n"
"	move.l (48,sp),a2\n"
"	move.l (52,sp),a3\n"
"	move.l (56,sp),a6\n"
"	move.l a2,a0\n"
"	jsr (-732,a6)\n"
"	jsr (a3)\n"
"	move.l d0,d2\n"
"	move.l a2,a0\n"
"	jsr (-732,a6)\n"
"	move.l d2,d0\n"
"	movem.l (sp)+,d2-d7/a2-a6\n"
"	rts\n");
int asb_on_stack(struct StackSwapStruct *sss, int (*fn)(void), struct ExecBase *base) __asm__("_asb_on_stack");
static int asb_main(int argc, char **argv);
static int g_main_argc; static char **g_main_argv;
static int asb_main_tramp(void){ return asb_main(g_main_argc,g_main_argv); }
int main(int argc, char **argv){
  static struct StackSwapStruct sss;
  struct Task *t=FindTask(NULL); volatile ULONG here=0;
  g_stk_in_sp=(ULONG)&here; g_stk_in_lo=(ULONG)t->tc_SPLower; g_stk_in_hi=(ULONG)t->tc_SPUpper;
  g_main_argc=argc; g_main_argv=argv;
  APTR mem=AllocVec(ASB_STACK,MEMF_ANY);
  if(!mem) return asb_main(argc,argv);
  g_stk_own=1;
  sss.stk_Lower=mem; sss.stk_Upper=(ULONG)mem+ASB_STACK; sss.stk_Pointer=(APTR)sss.stk_Upper;
  int rc=asb_on_stack(&sss,asb_main_tramp,SysBase);
  FreeVec(mem); return rc; }
