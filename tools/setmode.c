/* setmode W H D — pick best RTG mode, write ENV(ARC):Sys/screenmode.prefs; lists modes to SYS:ASB/modes.txt */
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void wl(unsigned char*p,unsigned long v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static void ww(unsigned char*p,unsigned v){p[0]=v>>8;p[1]=v;}
static int writeprefs(const char*path,ULONG id,int w,int h,int d){
  unsigned char b[64]; memset(b,0,sizeof b); int o=0;
  memcpy(b,"FORM",4); memcpy(b+8,"PREF",4); o=12;
  memcpy(b+o,"PRHD",4); wl(b+o+4,6); o+=8; o+=6;            /* version/type/flags = 0 */
  memcpy(b+o,"SCRM",4); wl(b+o+4,28); o+=8;
  o+=16; wl(b+o,id); o+=4; ww(b+o,w); o+=2; ww(b+o,h); o+=2; ww(b+o,d); o+=2; ww(b+o,1); o+=2; /* control: autoscroll */
  wl(b+4,o-8);
  FILE*f=fopen(path,"wb"); if(!f) return -1; fwrite(b,1,o,f); fclose(f); return 0;
}
int main(int argc,char**argv){
  int W=argc>1?atoi(argv[1]):1920,H=argc>2?atoi(argv[2]):1080,D=argc>3?atoi(argv[3]):16;
  FILE*lf=fopen("SYS:ASB/modes.txt","w");
  ULONG id=INVALID_ID, best=INVALID_ID;
  while((id=NextDisplayInfo(id))!=INVALID_ID){
    struct DimensionInfo di; struct NameInfo ni;
    if(!GetDisplayInfoData(NULL,(UBYTE*)&di,sizeof di,DTAG_DIMS,id)) continue;
    if(!GetDisplayInfoData(NULL,(UBYTE*)&ni,sizeof ni,DTAG_NAME,id)) strcpy((char*)ni.Name,"?");
    int w=di.Nominal.MaxX-di.Nominal.MinX+1,h=di.Nominal.MaxY-di.Nominal.MinY+1;
    if(lf) fprintf(lf,"%08lx %dx%d depth%d %s\n",(unsigned long)id,w,h,di.MaxDepth,ni.Name);
    if(w==W&&h==H&&di.MaxDepth>=D&&best==INVALID_ID) best=id;
  }
  if(best==INVALID_ID) best=BestModeID(BIDTAG_NominalWidth,W,BIDTAG_NominalHeight,H,BIDTAG_Depth,D,TAG_DONE);
  if(lf){ fprintf(lf,"chosen %08lx\n",(unsigned long)best); fclose(lf); }
  if(best==INVALID_ID) return 10;
  writeprefs("ENVARC:Sys/screenmode.prefs",best,W,H,D);
  writeprefs("ENV:Sys/screenmode.prefs",best,W,H,D);
  return 0;
}
