/* rmbclick: test helper for FS-UAE runs where the host cannot send a right mouse button (mouse integration
   forwards only the left one). Runs in the background (run rmbclick <trigger file>), and each time the trigger file
   appears it deletes it and sends a right-button press + release through input.device at the current pointer
   position, so a script on the host can do: move the pointer (xdotool), touch the file, take a screenshot.
   "<trigger>.down" / "<trigger>.up" only press / only release (MUI 5 shows menus while the button is held).
   A file "<trigger>.quit" ends it. (build: make build/amiga/rmbclick) */
#include <exec/types.h>
#include <exec/io.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
#include <stdio.h>
static int exists(const char *p){ BPTR l=Lock((STRPTR)p,ACCESS_READ); if(l){ UnLock(l); return 1; } return 0; }
static void send(struct IOStdReq *io,UWORD code){
  struct InputEvent ev; memset(&ev,0,sizeof ev);
  ev.ie_Class=IECLASS_RAWMOUSE; ev.ie_Code=code; ev.ie_Qualifier=IEQUALIFIER_RELATIVEMOUSE|((code&IECODE_UP_PREFIX)?0:IEQUALIFIER_RBUTTON);
  io->io_Command=IND_WRITEEVENT; io->io_Data=(APTR)&ev; io->io_Length=sizeof ev; DoIO((struct IORequest*)io); }
int main(int argc,char **argv){
  if(argc<2){ printf("usage: rmbclick <trigger file>\n"); return 5; }
  char quit[300], dn[300], upf[300]; snprintf(quit,sizeof quit,"%s.quit",argv[1]); snprintf(dn,sizeof dn,"%s.down",argv[1]); snprintf(upf,sizeof upf,"%s.up",argv[1]);
  struct MsgPort *mp=CreateMsgPort(); if(!mp) return 20;
  struct IOStdReq *io=(struct IOStdReq*)CreateIORequest(mp,sizeof(struct IOStdReq));
  if(!io||OpenDevice((STRPTR)"input.device",0,(struct IORequest*)io,0)){ printf("no input.device\n"); return 20; }
  int n=0;
  for(;;){
    if(SetSignal(0,SIGBREAKF_CTRL_C)&SIGBREAKF_CTRL_C) break;
    if(exists(quit)){ DeleteFile((STRPTR)quit); break; }
    if(exists(argv[1])){ DeleteFile((STRPTR)argv[1]); send(io,IECODE_RBUTTON); Delay(8); send(io,IECODE_RBUTTON|IECODE_UP_PREFIX); n++; }
    if(exists(dn)){ DeleteFile((STRPTR)dn); send(io,IECODE_RBUTTON); }
    if(exists(upf)){ DeleteFile((STRPTR)upf); send(io,IECODE_RBUTTON|IECODE_UP_PREFIX); n++; }
    Delay(10); }
  CloseDevice((struct IORequest*)io); DeleteIORequest((struct IORequest*)io); DeleteMsgPort(mp);
  printf("rmbclick: %d clicks\n",n); return 0; }
