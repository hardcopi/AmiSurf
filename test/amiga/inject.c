/* inject SECS [CODE] : after SECS seconds send one raw key (default 0x45 Esc) through input.device (emulator tests) */
#include <exec/types.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdlib.h>
#include <clib/alib_protos.h>
int main(int argc,char **argv){
  int secs=argc>1?atoi(argv[1]):10, code=argc>2?(int)strtol(argv[2],NULL,16):0x45;
  Delay(secs*50);
  struct MsgPort *mp=CreateMsgPort(); struct IOStdReq *io=(struct IOStdReq*)CreateIORequest(mp,sizeof *io);
  if(!io||OpenDevice((STRPTR)"input.device",0,(struct IORequest*)io,0)) return 20;
  struct InputEvent ie; int up;
  for(up=0;up<2;up++){
    ie.ie_NextEvent=NULL; ie.ie_Class=IECLASS_RAWKEY; ie.ie_SubClass=0; ie.ie_Code=(UWORD)(code|(up?IECODE_UP_PREFIX:0)); ie.ie_Qualifier=0;
    ie.ie_X=ie.ie_Y=0; ie.ie_TimeStamp.tv_secs=ie.ie_TimeStamp.tv_micro=0;
    io->io_Command=IND_WRITEEVENT; io->io_Data=&ie; io->io_Length=sizeof ie; DoIO((struct IORequest*)io); Delay(5); }
  CloseDevice((struct IORequest*)io); DeleteIORequest((struct IORequest*)io); DeleteMsgPort(mp); return 0; }
