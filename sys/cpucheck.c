/* AmiSurf 0.70: CPU/FPU check that runs before main() (after the AmiStudio Paint pattern).
 * Compiled with -m68020 -msoft-float so this file has no FPU instructions: on a machine without an FPU it shows a
 * requester and exits instead of crashing (or libnix's bare 0006 8881 alert). */
#include <exec/execbase.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <stdlib.h>
static char *cat(char *d, const char *s) { while (*d) d++; while ((*d = *s++)) d++; return d; }
static int asb_cpu_checked;
static int has_lib(const char *n) { Forbid(); int r = FindName(&SysBase->LibList, (STRPTR)n) != 0; Permit(); return r; }
static int ask(const char *body, const char *gads) {
    struct Library *ib = OpenLibrary((STRPTR)"intuition.library", 36); int r = 0;
    if (ib) { struct IntuitionBase *IntuitionBase = (struct IntuitionBase *)ib;
        struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"AmiSurf", (UBYTE *)body, (UBYTE *)gads };
        r = EasyRequestArgs(NULL, &es, NULL, NULL); (void)IntuitionBase; CloseLibrary(ib); }
    return r;
}
__attribute__((constructor)) static void asb_cpu_check(void) {
    if (asb_cpu_checked) return;
    asb_cpu_checked = 1;
    UWORD f = SysBase->AttnFlags;
    const char *cpu = (f & 0x80) ? "68060" : (f & AFF_68040) ? "68040" : (f & AFF_68030) ? "68030" : (f & AFF_68020) ? "68020" : (f & AFF_68010) ? "68010" : "68000";
    int fpu = (f & (AFF_68881 | AFF_68882 | AFF_FPU40)) != 0;
    int l040 = has_lib("68040.library"), l060 = has_lib("68060.library");
    char m[400]; m[0] = 0;
#ifdef ASP_020
    if (!(f & AFF_68020) || !fpu) {
        cat(m, "AmiSurf-020 needs a 68020 or better CPU with an FPU\n(68881/68882, or the 68040/68060 internal FPU).");
#else
    if (!(f & AFF_68040) || !fpu) {
        cat(m, "AmiSurf needs a 68040 or 68060 CPU with FPU.\nOn a 68020/68030 with an FPU use AmiSurf-020.");
#endif
        cat(m, "\nThis machine reports: "); cat(m, cpu); cat(m, fpu ? ", FPU yes." : ", no FPU."); cat(m, "\n\nIt cannot run here.");
        ask(m, "Quit"); exit(20);
    }
    if (((f & AFF_68040) && (f & 0x80) && !l060) || ((f & AFF_68040) && !(f & 0x80) && !l040 && !l060)) {
        cat(m, "The "); cat(m, cpu); cat(m, " support library ("); cat(m, (f & 0x80) ? "68060.library" : "68040.library");
        cat(m, ") is not loaded.\nSome CPU/FPU instructions then crash the machine.\nInstall it in LIBS: (SetPatch loads it) and reboot.\n\nStart anyway?");
        if (!ask(m, "Start anyway|Quit")) exit(20);
    }
}
/* libnix's -m68881 startup calls __fpucheck() before any constructor (bare Alert 0006 8881 without an FPU).
 * Ours replaces it: the requester above, then libnix's FPCR setup (round to nearest). */
void __fpucheck(void) {
    asb_cpu_check();
    __asm__ volatile ("fmove.l %%fpcr,%%d0\n\tandi.w #-49,%%d0\n\tfmove.l %%d0,%%fpcr" ::: "d0");
}
