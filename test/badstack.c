/* badstack: runs an Amiga program the way a beta tester's 0.87 logs suggest it was run (AmiStudio launcher, Kickstart 45.66
   on Amiberry): on a stack that is NOT the one the task's tc_SPLower/tc_SPUpper describe.
   usage: badstack <program> [args]   (build: make build/amiga/badstack)
   A 512 KB block: the program really runs at the bottom 128 KB (sp = block+128K), while the task fields claim a
   112 KB stack at block+400K..block+512K. libnix's __stkinit (AmiSurf 0.87) then copies 384 KB from tc_SPUpper down
   to sp into its 256 KB stack and ends up running below it; AmiSurf 0.88 swaps to a stack of its own and does not care. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
__asm__(
"	.text\n"
"	.even\n"
"_bs_call:\n"
"	movem.l d2-d7/a2-a6,-(sp)\n"
"	move.l (48,sp),a0\n"     /* new sp */
"	move.l (52,sp),a1\n"     /* entry */
"	move.l (56,sp),d2\n"     /* args */
"	move.l (60,sp),d0\n"     /* arg length */
"	move.l sp,-(a0)\n"
"	move.l a0,sp\n"
"	move.l d2,a0\n"
"	jsr (a1)\n"
"	move.l (sp),sp\n"
"	movem.l (sp)+,d2-d7/a2-a6\n"
"	rts\n");
LONG bs_call(APTR sp, APTR entry, STRPTR args, LONG len) __asm__("_bs_call");
int main(int argc,char **argv){
  if(argc<2){ printf("usage: badstack <program> [args]\n"); return 5; }
  BPTR seg=LoadSeg((STRPTR)argv[1]); if(!seg){ printf("cannot load %s\n",argv[1]); return 10; }
  static char args[512]; args[0]=0;
  for(int i=2;i<argc;i++){ strncat(args,argv[i],sizeof args-strlen(args)-3); strcat(args," "); }
  strcat(args,"\n");
  UBYTE *blk=AllocVec(512*1024,MEMF_ANY|MEMF_CLEAR); if(!blk){ UnLoadSeg(seg); return 20; }
  struct Process *pr=(struct Process*)FindTask(NULL);
  char dir[256]; strncpy(dir,argv[1],sizeof dir-1); dir[255]=0; *PathPart((STRPTR)dir)=0;
  BPTR home=Lock((STRPTR)(dir[0]?dir:""),ACCESS_READ), ohome=SetProgramDir(home);
  APTR olo=pr->pr_Task.tc_SPLower, ohi=pr->pr_Task.tc_SPUpper;
  printf("badstack: real stack %08lx-%08lx, task fields say %08lx-%08lx\n",(ULONG)blk,(ULONG)blk+131072,(ULONG)blk+409600,(ULONG)blk+524288);
  Forbid(); pr->pr_Task.tc_SPLower=blk+409600; pr->pr_Task.tc_SPUpper=blk+524288; Permit();
  LONG rc=bs_call(blk+131072-16,(APTR)((BADDR(seg))+4),(STRPTR)args,(LONG)strlen(args));
  Forbid(); pr->pr_Task.tc_SPLower=olo; pr->pr_Task.tc_SPUpper=ohi; Permit();
  SetProgramDir(ohome); UnLock(home);
  printf("badstack: %s returned %ld\n",argv[1],(long)rc);
  FreeVec(blk); UnLoadSeg(seg); return 0; }
