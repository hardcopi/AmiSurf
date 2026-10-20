/* inject2 "SCRIPT": emulator input driver. Words: wN wait N ticks (50/s), kHH raw key, aHH Amiga+raw key,
   sTEXT type lowercase text, pX,Y pointer to screen X,Y, r right click, l left click, R right down, U right up */
#include <exec/types.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdlib.h>
#include <string.h>
#include <clib/alib_protos.h>
static struct IOStdReq *io;
static void ev(UBYTE cl,UWORD code,UWORD q,WORD x,WORD y){ struct InputEvent ie; memset(&ie,0,sizeof ie);
  ie.ie_Class=cl; ie.ie_Code=code; ie.ie_Qualifier=q; ie.ie_X=x; ie.ie_Y=y;
  io->io_Command=IND_WRITEEVENT; io->io_Data=&ie; io->io_Length=sizeof ie; DoIO((struct IORequest*)io); }
static void key(int c,UWORD q){ ev(IECLASS_RAWKEY,(UWORD)c,q,0,0); Delay(2); ev(IECLASS_RAWKEY,(UWORD)(c|IECODE_UP_PREFIX),q,0,0); Delay(2); }
static int rawof(char c){ const char *r1="qwertyuiop",*r2="asdfghjkl",*r3="zxcvbnm"; const char *p;
  if((p=strchr(r1,c))) return 0x10+(int)(p-r1); if((p=strchr(r2,c))) return 0x20+(int)(p-r2); if((p=strchr(r3,c))) return 0x31+(int)(p-r3);
  if(c=='.') return 0x39; if(c==' ') return 0x40; return -1; }
int main(int argc,char **argv){
  if(argc<2) return 5;
  struct MsgPort *mp=CreateMsgPort(); io=(struct IOStdReq*)CreateIORequest(mp,sizeof *io);
  if(!io||OpenDevice((STRPTR)"input.device",0,(struct IORequest*)io,0)) return 20;
  char *s=argv[1];
  while(*s){ while(*s==' ') s++; if(!*s) break; char c=*s++; char w[200]; int n=0; while(*s && *s!=' ' && n<199) w[n++]=*s++; w[n]=0;
    if(c=='w') Delay(atoi(w));
    else if(c=='k') key((int)strtol(w,NULL,16),0);
    else if(c=='a') key((int)strtol(w,NULL,16),IEQUALIFIER_RCOMMAND);
    else if(c=='c') key((int)strtol(w,NULL,16),IEQUALIFIER_CONTROL);
    else if(c=='m'){ ev(IECLASS_RAWMOUSE,IECODE_RBUTTON,IEQUALIFIER_RBUTTON,0,0); Delay((LONG)atoi(w)); ev(IECLASS_RAWMOUSE,IECODE_RBUTTON|IECODE_UP_PREFIX,0,0,0); }
    else if(c=='s'){ for(int i=0;w[i];i++){ int r=rawof(w[i]); if(r>=0){ key(r,0); Delay(8); } } }
    else if(c=='p'){ int x=atoi(w); char *cm=strchr(w,','); int y=cm?atoi(cm+1):0; ev(IECLASS_POINTERPOS,IECODE_NOBUTTON,0,(WORD)x,(WORD)y); Delay(2); }
    else if(c=='r'){ ev(IECLASS_RAWMOUSE,IECODE_RBUTTON,IEQUALIFIER_RBUTTON,0,0); Delay(5); ev(IECLASS_RAWMOUSE,IECODE_RBUTTON|IECODE_UP_PREFIX,0,0,0); }
    else if(c=='l'){ ev(IECLASS_RAWMOUSE,IECODE_LBUTTON,IEQUALIFIER_LEFTBUTTON,0,0); Delay(5); ev(IECLASS_RAWMOUSE,IECODE_LBUTTON|IECODE_UP_PREFIX,0,0,0); }
    else if(c=='R') ev(IECLASS_RAWMOUSE,IECODE_RBUTTON,IEQUALIFIER_RBUTTON,0,0);
    else if(c=='U') ev(IECLASS_RAWMOUSE,IECODE_RBUTTON|IECODE_UP_PREFIX,0,0,0); }
  CloseDevice((struct IORequest*)io); DeleteIORequest((struct IORequest*)io); DeleteMsgPort(mp); return 0; }
