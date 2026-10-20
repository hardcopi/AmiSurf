#include "asbc.h"
#include <errno.h>
#include <string.h>
#ifdef __AMIGA__
#include <proto/socket.h>
#include <sys/ioctl.h>
extern struct Library *SocketBase;
#define sock_read(fd,b,n) recv((fd),(char*)(b),(int)(n),0)
#define sock_write(fd,b,n) send((fd),(char*)(b),(int)(n),0)
#else
#include <unistd.h>
#include <fcntl.h>
#define sock_read read
#define sock_write write
#endif

void asbc_put_u16(unsigned char *p, unsigned v){ p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8); }
unsigned asbc_get_u16(const unsigned char *p){ return (unsigned)p[0]|((unsigned)p[1]<<8); }
void asbc_put_i16(unsigned char *p, int v){ asbc_put_u16(p,(unsigned)(int16_t)v); }
int asbc_get_i16(const unsigned char *p){ return (int)(int16_t)asbc_get_u16(p); }

static int wr_all(int fd, const void *buf, size_t n){
  const unsigned char *p=buf;
  while(n){ ssize_t k=sock_write(fd,p,n); if(k<0){ if(errno==EINTR) continue; return -1; } if(!k) return -1; p+=k; n-=(size_t)k; }
  return 0; }
static int rd_all(int fd, void *buf, size_t n){
  unsigned char *p=buf;
  while(n){ ssize_t k=sock_read(fd,p,n); if(k<0){ if(errno==EINTR) continue; return -1; } if(!k) return -2; p+=k; n-=(size_t)k; }
  return 0; }

int asbc_handshake_client(int fd, const char *token){
  unsigned char b[7+ASBC_MAX_TOKEN]; size_t n=token?strlen(token):0; if(n>ASBC_MAX_TOKEN) n=ASBC_MAX_TOKEN;
  memcpy(b,ASBC_MAGIC,4); asbc_put_u16(b+4,ASBC_VERSION); b[6]=(unsigned char)n; if(n) memcpy(b+7,token,n);
  if(wr_all(fd,b,7+n)) return -1;
  if(rd_all(fd,b,7)) return -1;
  if(memcmp(b,ASBC_MAGIC,4)||asbc_get_u16(b+4)!=ASBC_VERSION) return -1;
  return b[6]?-2:0; }
int asbc_handshake_server(int fd, const char *token){
  unsigned char b[7], t[ASBC_MAX_TOKEN+1];
  if(rd_all(fd,b,7)) return -1;
  if(memcmp(b,ASBC_MAGIC,4)||asbc_get_u16(b+4)!=ASBC_VERSION) return -1;
  size_t n=b[6]; if(n>ASBC_MAX_TOKEN) return -1;
  if(n && rd_all(fd,t,n)) return -1;
  t[n]=0;
  int ok=1;
  if(token && *token){ /* constant-time compare */
    size_t tl=strlen(token); unsigned diff=(unsigned)(tl^n);
    for(size_t i=0;i<tl;i++) diff|=(unsigned)(token[i]^(i<n?t[i]:0));
    ok=diff==0; }
  memcpy(b,ASBC_MAGIC,4); asbc_put_u16(b+4,ASBC_VERSION); b[6]=ok?0:1;
  if(wr_all(fd,b,7)) return -1;
  return ok?0:-2; }

int asbc_send(int fd, int op, const void *p, uint32_t n){
  unsigned char hdr[5];
  uint32_t tot=n+1;
  hdr[0]=(unsigned char)tot; hdr[1]=(unsigned char)(tot>>8);
  hdr[2]=(unsigned char)(tot>>16); hdr[3]=(unsigned char)(tot>>24);
  hdr[4]=(unsigned char)op;
  if(wr_all(fd,hdr,5)) return -1;
  if(n && wr_all(fd,p,n)) return -1;
  return 0; }

static int recv_body(int fd, int *op, void *buf, uint32_t cap, unsigned char *ln){
  uint32_t tot=(uint32_t)ln[0]|((uint32_t)ln[1]<<8)|((uint32_t)ln[2]<<16)|((uint32_t)ln[3]<<24);
  if(tot<1||tot>ASBC_MAX_MSG) return -1;
  unsigned char opc; int r=rd_all(fd,&opc,1); if(r) return r;
  *op=opc;
  uint32_t n=tot-1;
  if(n>cap){
    unsigned char junk[4096];
    while(n){ uint32_t c=n>sizeof junk?(uint32_t)sizeof junk:n; if(rd_all(fd,junk,c)) return -1; n-=c; }
    return -1; }
  if(n && (r=rd_all(fd,buf,n))) return r;
  return (int)n; }

int asbc_recv(int fd, int *op, void *buf, uint32_t cap){
  unsigned char ln[4];
  int r=rd_all(fd,ln,4); if(r) return r;
  return recv_body(fd,op,buf,cap,ln); }

int asbc_recv_nb(int fd, int *op, void *buf, uint32_t cap){
  *op=-1; /* stays -1 when nothing was waiting (0 alone is a valid empty message) */
#ifdef __AMIGA__
  long nb=1; IoctlSocket(fd,FIONBIO,(char*)&nb);
  unsigned char ln[4];
  ssize_t k=sock_read(fd,ln,4);
  nb=0; IoctlSocket(fd,FIONBIO,(char*)&nb);
  if(k<0){ if(errno==EAGAIN||errno==EWOULDBLOCK) return 0; return -1; }
#else
  int fl=fcntl(fd,F_GETFL,0);
  if(fl<0) return asbc_recv(fd,op,buf,cap);
  fcntl(fd,F_SETFL,fl|O_NONBLOCK);
  unsigned char ln[4];
  ssize_t k=sock_read(fd,ln,4);
  fcntl(fd,F_SETFL,fl);
  if(k<0){ if(errno==EAGAIN||errno==EWOULDBLOCK) return 0; return -1; }
#endif
  if(k==0) return -2;
  if(k<4){ int r=rd_all(fd,ln+k,4-(int)k); if(r) return r; }
  return recv_body(fd,op,buf,cap,ln); }
