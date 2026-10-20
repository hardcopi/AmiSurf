/* asbc_test — ASBC handshake/message round trip over a socketpair + QOI encode/decode round trip. */
#define QOI_IMPLEMENTATION
#include "../chrome/qoi.h"
#include "../chrome/asbc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
static int fails=0;
#define CHECK(c,msg) do{ if(c) printf("ok   %s\n",msg); else { printf("FAIL %s\n",msg); fails++; } }while(0)
int main(void){
  unsigned char b[64]; asbc_put_u16(b,0xBEEF); asbc_put_i16(b+2,-1234);
  CHECK(asbc_get_u16(b)==0xBEEF && b[0]==0xEF,"u16 little-endian");
  CHECK(asbc_get_i16(b+2)==-1234,"i16 sign");
  int sv[2]; if(socketpair(AF_UNIX,SOCK_STREAM,0,sv)){ perror("socketpair"); return 1; }
  pid_t pid=fork();
  if(!pid){ close(sv[0]); if(asbc_handshake_server(sv[1],"s3cret-token")) _exit(2);
    int op; static unsigned char buf[1<<16]; int n=asbc_recv(sv[1],&op,buf,sizeof buf);
    if(n<0) _exit(3); asbc_send(sv[1],op+100,buf,(uint32_t)n); _exit(0); }
  close(sv[1]);
  CHECK(asbc_handshake_client(sv[0],"s3cret-token")==0,"handshake with token");
  const char *url="https://www.google.com/";
  CHECK(asbc_send(sv[0],ASBC_NAV,url,(uint32_t)strlen(url))==0,"send NAV");
  int op=0; static unsigned char rb[1<<16]; int n=asbc_recv(sv[0],&op,rb,sizeof rb);
  CHECK(op==ASBC_NAV+100 && n==(int)strlen(url) && !memcmp(rb,url,(size_t)n),"echo round trip");
  int st; waitpid(pid,&st,0); CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==0,"server side");
  /* empty-payload messages (BACK/RELOAD/TABNEW) must not look like "nothing waiting" */
  { int p3[2]; socketpair(AF_UNIX,SOCK_STREAM,0,p3); int o=0; unsigned char t[8];
    CHECK(asbc_recv_nb(p3[0],&o,t,sizeof t)==0 && o==-1,"recv_nb: nothing waiting -> op -1");
    asbc_send(p3[1],ASBC_BACK,NULL,0);
    CHECK(asbc_recv_nb(p3[0],&o,t,sizeof t)==0 && o==ASBC_BACK,"recv_nb: empty BACK message delivered");
    asbc_send(p3[1],ASBC_RELOAD,NULL,0);
    CHECK(asbc_recv(p3[0],&o,t,sizeof t)==0 && o==ASBC_RELOAD,"recv: empty RELOAD message delivered");
    close(p3[0]); close(p3[1]); }
  /* wrong token, empty token, open server */
  const char *cases[][3]={{"right-token","wrong-token","-2"},{"right-token","","-2"},{"","anything","0"},{NULL,NULL,"0"}};
  for(int c=0;c<4;c++){
    int p2[2]; socketpair(AF_UNIX,SOCK_STREAM,0,p2); pid_t q=fork();
    if(!q){ close(p2[0]); int r=asbc_handshake_server(p2[1],cases[c][0]); _exit(r==-2?9:r?2:0); }
    close(p2[1]); int r=asbc_handshake_client(p2[0],cases[c][1]); close(p2[0]);
    int st2; waitpid(q,&st2,0); char msg[96];
    snprintf(msg,sizeof msg,"token case %d -> client %d (want %s)",c,r,cases[c][2]);
    CHECK(r==atoi(cases[c][2]) && WEXITSTATUS(st2)==(r==-2?9:0),msg); }
  /* QOI */
  int w=37,h=23; unsigned char *px=malloc((size_t)w*h*4);
  for(int i=0;i<w*h;i++){ px[i*4]=(unsigned char)(i*7); px[i*4+1]=(unsigned char)(i/3); px[i*4+2]=(unsigned char)(i%5?40:200); px[i*4+3]=255; }
  qoi_desc d={ (unsigned)w,(unsigned)h,4,QOI_SRGB }; int len=0; void *enc=qoi_encode(px,&d,&len);
  qoi_desc d2; void *dec=enc?qoi_decode(enc,len,&d2,4):NULL;
  CHECK(dec && d2.width==(unsigned)w && d2.height==(unsigned)h && !memcmp(dec,px,(size_t)w*h*4),"qoi round trip");
  { unsigned char raw[64*3], dec[64*3]; int i; for(i=0;i<64*3;i++) raw[i]=(unsigned char)(i<90?7:i*13); /* runs + literals */
    unsigned char pk[]={130,1,2,3, 2,4,5,6,7,8,9,10,11,12}; int c=asbc_unrle(pk,sizeof pk,dec,7,3);
    CHECK(c==(int)sizeof pk && dec[0]==1&&dec[9]==1&&dec[12]==4&&dec[20]==12,"RLE decode runs+literals"); (void)raw;
    CHECK(asbc_unrle(pk,5,dec,7,3)<0,"RLE rejects truncated data"); }
  free(enc); free(dec); free(px);
  /* 0.68 ops: distinct codes, CMD text and FINDRES/ZOOM payloads round trip */
  { int ops[]={ASBC_CMD,ASBC_CTX,ASBC_SUGG,ASBC_ZOOM,ASBC_FINDRES}, dup=0, i, j;
    for(i=0;i<5;i++) for(j=i+1;j<5;j++) if(ops[i]==ops[j]) dup=1;
    CHECK(!dup && ASBC_CMD<32 && ASBC_CTX>=32,"0.68 op codes distinct, client->host below 32");
    int p4[2]; socketpair(AF_UNIX,SOCK_STREAM,0,p4); int o=0; static unsigned char t[256];
    const char *c="tabmove 2 0"; asbc_send(p4[0],ASBC_CMD,c,(uint32_t)strlen(c));
    int m=asbc_recv(p4[1],&o,t,sizeof t); CHECK(o==ASBC_CMD && m==(int)strlen(c) && !memcmp(t,c,(size_t)m),"CMD text round trip");
    unsigned char f[4]; asbc_put_u16(f,3); asbc_put_u16(f+2,17); asbc_send(p4[1],ASBC_FINDRES,f,4);
    m=asbc_recv(p4[0],&o,t,sizeof t); CHECK(o==ASBC_FINDRES && m==4 && asbc_get_u16(t)==3 && asbc_get_u16(t+2)==17,"FINDRES 3/17");
    close(p4[0]); close(p4[1]); }
  printf("%s (%d failed)\n",fails?"FAILED":"all passed",fails); return fails?1:0; }
