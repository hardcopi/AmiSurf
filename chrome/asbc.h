/* ASBC v2 — AmiSurf Chrome wire protocol.
   Handshake: client sends "ASBC" + u16le version (2) + u8 n + n bytes token (n may be 0);
   server answers "ASBC" + u16le version + u8 status (0 ok, 1 bad token).
   Then messages: u32le nbytes, then nbytes = u8 op + payload. */
#ifndef ASBC_H
#define ASBC_H
#include <stdint.h>
#include <stddef.h>

#define ASBC_MAGIC "ASBC"
#define ASBC_VERSION 2
#define ASBC_MAX_TOKEN 64
#define ASBC_PORT 9223
#define ASBC_MAX_MSG (8*1024*1024)

enum {
  ASBC_HELLO  = 1,  /* u16le w, u16le h */
  ASBC_NAV    = 2,  /* utf-8 url */
  ASBC_BACK   = 3,
  ASBC_FWD    = 4,
  ASBC_RELOAD = 5,
  ASBC_STOP   = 6,
  ASBC_MOUSE  = 7,  /* u16 x, u16 y, u8 buttons, u8 type (0 move 1 down 2 up) */
  ASBC_WHEEL  = 8,  /* i16 dy */
  ASBC_KEY    = 9,  /* u8 type (0 down 1 up 2 char), u16 vk, u8 mods, u16 n, utf8 */
  ASBC_RESIZE = 10, /* u16 w, u16 h */
  ASBC_COPY   = 11,
  ASBC_PASTE  = 12, /* utf-8 */
  ASBC_FIND   = 13, /* utf-8 query; empty = find next */
  ASBC_TABNEW = 15,
  ASBC_TABCLS = 16, /* u8 index */
  ASBC_TABSEL = 17, /* u8 index */
  ASBC_CMD    = 18, /* utf-8 "verb args": zoom +|-|0|N, findnext, findprev, findclose, ctx x y, newtab URL,
                       saveimg URL, savepng, savehtml, savepdf, source, reopen, tabmove FROM TO, tabnext, tabprev,
                       bookmark, suggest PREFIX, page bookmarks|history|downloads|settings|passwords */
  ASBC_FMT    = 19, /* u8 pixfmt (ASBC_PF_*), u8 flags (ASBC_FF_*), u16 npal, npal*3 RGB palette (CLUT8 only).
                       Sent once after HELLO; from then on FRAME tiles come as fmt 3 (raw, client pixel format). */
  ASBC_AUDIOCTL=21, /* u8 1 play 0 mute (client -> host; host stops streaming while muted) */
  ASBC_ACK    = 20, /* empty: one frame/scroll applied (only when ASBC_FF_ACK was announced) */
  ASBC_FRAME  = 32, /* u16 cssw,cssh, x,y,tw,th, u8 fmt (2=qoi), data */
  ASBC_META   = 33, /* u8 flags (1 back 2 fwd 4 loading), u16 nurl, url, u16 ntitle, title */
  ASBC_CURSOR = 34, /* u8 0 arrow 1 ibeam 2 hand 3 wait */
  ASBC_STATUS = 35, /* utf-8 */
  ASBC_CLIP   = 36, /* utf-8 selection */
  ASBC_TABS   = 37, /* u8 n, u8 active, n * (u16 ntitle, title) */
  ASBC_CTX    = 38, /* utf-8 "link\nimage\nselection" (reply to "ctx x y") */
  ASBC_SUGG   = 39, /* utf-8 lines "url\ttitle" (reply to "suggest PREFIX") */
  ASBC_ZOOM   = 40, /* u16 percent */
  ASBC_FINDRES= 41, /* u16 current (1-based, 0 none), u16 total */
  ASBC_STATS  = 42, /* u16 frames sent last second, u32 bytes sent last second, u32 host encode us per frame (avg) */
  ASBC_AUDIO  = 44, /* u8 fmt (ASBC_AU_*), u8 channels, u16 rate, u32 host ms timestamp, u16 nframes, data */
  ASBC_BMARKS = 45, /* 0.87, host -> client: utf-8 lines "url\ttitle\tfolder" (bookmarks.tsv order); sent after the
                       client's "bookmarks" cmd and again whenever the list changes. Older clients never ask. */
  ASBC_FAVICON= 46, /* 0.88, host -> client: u8 nhost, host, u8 w, u8 h, w*h*4 RGBA (0x0: none); reply to "favicon HOST" */
  ASBC_SCROLL = 43  /* u16 x,y,w,h, i16 dy: move that page area by dy (content moves up when dy<0) */
};

/* FRAME fmt 3 payload: u8 comp (0 raw, 1 RLE) then tw*th pixels in the client's format.
   RLE per pixel: c<128 -> c+1 literal pixels follow; c>=128 -> next pixel repeated c-126 times (2..129). */
enum { ASBC_PF_QOI=0, ASBC_PF_RGB24=1, ASBC_PF_RGB565=2, ASBC_PF_RGB565PC=3, ASBC_PF_RGB555=4, ASBC_PF_RGB555PC=5,
       ASBC_PF_ARGB32=6, ASBC_PF_BGRA32=7, ASBC_PF_RGBA32=8, ASBC_PF_CLUT8=9, ASBC_PF_BGR24=10, ASBC_PF_ABGR32=11,
       ASBC_PF_BGR565PC=12, ASBC_PF_BGR555PC=13, ASBC_PF_N };
/* HELLO byte 5 (optional, 0.74+): audio formats the client can play; 0/absent = no audio is sent */
enum { ASBC_AF_S8=1, ASBC_AF_ADPCM=2, ASBC_AF_S16=4 };
/* AUDIO fmt: 0 silence (no data), 1 s8 interleaved, 2 IMA ADPCM (per channel s16be pred,u8 index,u8 0; then one byte
   per frame: low nibble left, high nibble right), 3 s16be interleaved */
enum { ASBC_AU_SIL=0, ASBC_AU_S8=1, ASBC_AU_ADPCM=2, ASBC_AU_S16=3 };
#define ASBC_AU_RATE 22050
static const short asbc_ima_step[89]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,
  157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,
  2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,
  22385,24623,27086,29794,32767};
static const signed char asbc_ima_idx[16]={-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
/* decode one nibble; pred and idx carry the state */
static inline int asbc_ima_dec(int nib,int *pred,int *idx){
  int st=asbc_ima_step[*idx], d=st>>3; if(nib&4) d+=st; if(nib&2) d+=st>>1; if(nib&1) d+=st>>2;
  int p=*pred+((nib&8)?-d:d); if(p>32767) p=32767; if(p<-32768) p=-32768; *pred=p;
  int i=*idx+asbc_ima_idx[nib&15]; if(i<0) i=0; if(i>88) i=88; *idx=i; return p; }
enum { ASBC_FF_RLE=1, ASBC_FF_ACK=2, ASBC_FF_SCROLL=4, ASBC_FF_STATS=8 };
static inline int asbc_pf_bpp(int pf){ static const unsigned char b[ASBC_PF_N]={3,3,2,2,2,2,4,4,4,1,3,4,2,2}; return pf>=0&&pf<ASBC_PF_N?b[pf]:3; }
/* undo the fmt 3 RLE; returns bytes consumed or -1 on bad data */
static inline int asbc_unrle(const unsigned char *s,int n,unsigned char *d,int npix,int bpp){
  const unsigned char *s0=s, *se=s+n; unsigned char *de=d+(long)npix*bpp;
  while(d<de){
    if(s>=se) return -1;
    int c=*s++;
    if(c<128){ int k=(c+1)*bpp; if(s+k>se||d+k>de) return -1; while(k--) *d++=*s++; }
    else { int r=c-126; if(s+bpp>se||d+(long)r*bpp>de) return -1;
      if(bpp==2){ unsigned char a=s[0],b=s[1]; while(r--){ d[0]=a; d[1]=b; d+=2; } }
      else if(bpp==1){ unsigned char a=s[0]; while(r--) *d++=a; }
      else { while(r--){ int k; for(k=0;k<bpp;k++) d[k]=s[k]; d+=bpp; } }
      s+=bpp; } }
  return (int)(s-s0); }
enum { ASBC_MMOVE=0, ASBC_MDOWN=1, ASBC_MUP=2 };
enum { ASBC_KDOWN=0, ASBC_KUP=1, ASBC_KCHAR=2 };

/* returns 0 ok, -1 io/protocol error, -2 token rejected */
int asbc_handshake_client(int fd, const char *token);
/* token NULL or "" = accept any client token */
int asbc_handshake_server(int fd, const char *token);
int asbc_send(int fd, int op, const void *p, uint32_t n);
int asbc_recv(int fd, int *op, void *buf, uint32_t cap);
/* non-blocking: returns 0 with *op==-1 when no message is waiting; 0 with *op>=0 is a message with no payload */
int asbc_recv_nb(int fd, int *op, void *buf, uint32_t cap);

void asbc_put_u16(unsigned char *p, unsigned v);
unsigned asbc_get_u16(const unsigned char *p);
void asbc_put_i16(unsigned char *p, int v);
int asbc_get_i16(const unsigned char *p);

#endif
