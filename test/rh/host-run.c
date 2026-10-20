/* stand-in for Amiberry's C:host-run / C:host-path under FS-UAE (no uae.resource there):
   host-run writes its command line to SYS:T/hostrun.req for test/rh/rh-bridge.sh on Linux, waits until it is taken;
   built as host-path it succeeds only for paths on the System: directory drive (what the real tool can map). */
#include <stdio.h>
#include <string.h>
#include <proto/dos.h>
int main(int argc,char **argv){
#ifdef HOSTPATH
  return (argc>1 && !strncasecmp(argv[1],"System:",7)) ? 0 : 10;
#else
  FILE *f=fopen("SYS:T/hostrun.tmp","w"); if(!f) return 10;
  for(int i=1;i<argc;i++) fprintf(f,"%s%s",argv[i],i+1<argc?"\x1f":"\n");
  fclose(f); Rename((STRPTR)"SYS:T/hostrun.tmp",(STRPTR)"SYS:T/hostrun.req");
  for(int i=0;i<250;i++){ BPTR l=Lock((STRPTR)"SYS:T/hostrun.req",ACCESS_READ); if(!l) return 0; UnLock(l); Delay(2); }
  return 10;
#endif
}
