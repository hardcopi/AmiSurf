/* frames.inc — pre-converted frames for the client's screen format (0.69). Included by host.c after send_qoi_tile.
   The client announces its pixel format with ASBC_FMT; changed areas are converted here (RGB565/555 either byte
   order, 24/32-bit orders, or CLUT8 with an ordered dither onto the client's palette) and sent raw or RLE-packed,
   so the 68k only unpacks runs and copies to the screen. QOI stays the fallback (pixfmt 0 / old clients). */
static int g_pf=ASBC_PF_QOI, g_ff;                  /* client pixel format + flags */
static unsigned char g_pal[256*3]; static int g_npal;
static unsigned char *g_lut;                         /* 32x32x32 RGB -> palette index */
static unsigned char *g_conv; static size_t g_convn; /* converted tile */
static unsigned char *g_rle;  static size_t g_rlen;

static void pf_reset(void){ g_pf=ASBC_PF_QOI; g_ff=0; g_npal=0; free(g_lut); g_lut=NULL; }

static void lut_build(void){
  free(g_lut); g_lut=malloc(32*32*32); if(!g_lut) return;
  for(int r=0;r<32;r++) for(int g=0;g<32;g++) for(int b=0;b<32;b++){
    int R=r*255/31, G=g*255/31, B=b*255/31, best=0; long bd=1L<<40;
    for(int i=0;i<g_npal;i++){ long dr=R-g_pal[i*3], dg=G-g_pal[i*3+1], db=B-g_pal[i*3+2];
      long d=dr*dr*3+dg*dg*4+db*db*2; if(d<bd){ bd=d; best=i; } }
    g_lut[(r<<10)|(g<<5)|b]=(unsigned char)best; } }

static void on_fmt(const unsigned char *p,int n){
  if(n<4) return;
  int pf=p[0]; g_ff=p[1]; int np=(int)asbc_get_u16(p+2);
  if(pf>=ASBC_PF_N) pf=ASBC_PF_QOI;
  if(pf==ASBC_PF_CLUT8){ if(np<2||np>256||n<4+np*3) pf=ASBC_PF_QOI; else { memcpy(g_pal,p+4,(size_t)np*3); g_npal=np; lut_build(); if(!g_lut) pf=ASBC_PF_QOI; } }
  g_pf=pf;
  fprintf(stderr,"asb-chromed: client pixel format %d (%d bpp) flags %d%s\n",g_pf,asbc_pf_bpp(g_pf)*8,g_ff,g_pf==ASBC_PF_CLUT8?" + palette":"");
  free(g_prev); g_prev=NULL; /* resend everything in the new format */
}

static const unsigned char bayer4h[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
/* convert tile (x,y,tw,th) of rgb (stride) into g_conv in the client's format */
static int conv_tile(int x,int y,int tw,int th,const unsigned char *rgb,int stride){
  int bpp=asbc_pf_bpp(g_pf); size_t need=(size_t)tw*th*bpp;
  if(need>g_convn){ free(g_conv); g_conv=malloc(need); g_convn=g_conv?need:0; if(!g_conv) return -1; }
  unsigned char *d=g_conv;
  for(int j=0;j<th;j++){
    const unsigned char *s=rgb+(size_t)(y+j)*stride+(size_t)x*3;
    const unsigned char *bm=bayer4h+(((y+j)&3)<<2);
    for(int i=0;i<tw;i++,s+=3){
      unsigned r=s[0],g=s[1],b=s[2], v;
      switch(g_pf){
      case ASBC_PF_RGB24: d[0]=r; d[1]=g; d[2]=b; d+=3; break;
      case ASBC_PF_BGR24: d[0]=b; d[1]=g; d[2]=r; d+=3; break;
      case ASBC_PF_RGB565: v=((r>>3)<<11)|((g>>2)<<5)|(b>>3); d[0]=v>>8; d[1]=v; d+=2; break;
      case ASBC_PF_RGB565PC: v=((r>>3)<<11)|((g>>2)<<5)|(b>>3); d[0]=v; d[1]=v>>8; d+=2; break;
      case ASBC_PF_BGR565PC: v=((b>>3)<<11)|((g>>2)<<5)|(r>>3); d[0]=v; d[1]=v>>8; d+=2; break;
      case ASBC_PF_RGB555: v=((r>>3)<<10)|((g>>3)<<5)|(b>>3); d[0]=v>>8; d[1]=v; d+=2; break;
      case ASBC_PF_RGB555PC: v=((r>>3)<<10)|((g>>3)<<5)|(b>>3); d[0]=v; d[1]=v>>8; d+=2; break;
      case ASBC_PF_BGR555PC: v=((b>>3)<<10)|((g>>3)<<5)|(r>>3); d[0]=v; d[1]=v>>8; d+=2; break;
      case ASBC_PF_ARGB32: d[0]=255; d[1]=r; d[2]=g; d[3]=b; d+=4; break;
      case ASBC_PF_ABGR32: d[0]=255; d[1]=b; d[2]=g; d[3]=r; d+=4; break;
      case ASBC_PF_BGRA32: d[0]=b; d[1]=g; d[2]=r; d[3]=255; d+=4; break;
      case ASBC_PF_RGBA32: d[0]=r; d[1]=g; d[2]=b; d[3]=255; d+=4; break;
      case ASBC_PF_CLUT8: { int t=((int)bm[(x+i)&3]-8)*3; /* +-24: ordered dither before the 5-bit lookup */
        int R=(int)r+t, G=(int)g+t, B=(int)b+t; R=R<0?0:R>255?255:R; G=G<0?0:G>255?255:G; B=B<0?0:B>255?255:B;
        *d++=g_lut[((R>>3)<<10)|((G>>3)<<5)|(B>>3)]; break; }
      } } }
  return (int)need; }

/* per-pixel RLE (see asbc.h); returns packed size, or -1 if it would not be smaller than raw */
static int rle_pack(const unsigned char *s,int npix,int bpp){
  size_t cap=(size_t)npix*bpp+(size_t)npix/128+16;
  if(cap>g_rlen){ free(g_rle); g_rle=malloc(cap); g_rlen=g_rle?cap:0; if(!g_rle) return -1; }
  unsigned char *o=g_rle, *lit=NULL; int litn=0, i=0, lim=npix*bpp;
  while(i<npix){
    const unsigned char *px=s+(size_t)i*bpp; int r=1;
    while(i+r<npix && r<129 && !memcmp(px+(size_t)r*bpp,px,(size_t)bpp)) r++;
    if(r>=2){ *o++=(unsigned char)(r+126); memcpy(o,px,(size_t)bpp); o+=bpp; i+=r; litn=0; }
    else { if(!litn){ lit=o++; } *lit=(unsigned char)litn; memcpy(o,px,(size_t)bpp); o+=bpp; i++; if(++litn==128) litn=0; }
    if(o-g_rle>=lim) return -1; }
  return (int)(o-g_rle); }

static long long g_enc_us; static int g_enc_n; static long g_sent_by; static int g_sent_fr; /* stats, per second */
static long long us_now(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (long long)t.tv_sec*1000000+t.tv_nsec/1000; }

/* send one changed tile in the client's format (or QOI) */
static int send_tile(int cssw,int cssh,int x,int y,int tw,int th,const unsigned char *rgb,int stride){
  long long t0=us_now(); int rc;
  if(g_pf==ASBC_PF_QOI) rc=send_qoi_tile(cssw,cssh,x,y,tw,th,rgb,stride);
  else {
    int bpp=asbc_pf_bpp(g_pf), raw=conv_tile(x,y,tw,th,rgb,stride);
    if(raw<0) return -1;
    int pk=(g_ff&ASBC_FF_RLE)?rle_pack(g_conv,tw*th,bpp):-1;
    const unsigned char *data=pk>0?g_rle:g_conv; int dn=pk>0?pk:raw;
    if(15+dn>(int)sizeof g_pkt){ /* too big for one message: split in two halves */
      int h=th/2; if(h<1) return -1;
      rc=send_tile(cssw,cssh,x,y,tw,h,rgb,stride); if(!rc) rc=send_tile(cssw,cssh,x,y+h,tw,th-h,rgb,stride); return rc; }
    unsigned char *p=g_pkt;
    asbc_put_u16(p,(unsigned)cssw); asbc_put_u16(p+2,(unsigned)cssh);
    asbc_put_u16(p+4,(unsigned)x); asbc_put_u16(p+6,(unsigned)y);
    asbc_put_u16(p+8,(unsigned)tw); asbc_put_u16(p+10,(unsigned)th);
    p[12]=3; p[13]=(unsigned char)g_pf; p[14]=pk>0?1:0;
    memcpy(p+15,data,(size_t)dn);
    if(getenv("ASB_DEBUG")) fprintf(stderr,"asb-chromed: raw pf%d %s %d bytes tile %dx%d @%d,%d\n",g_pf,pk>0?"rle":"raw",dn,tw,th,x,y);
    rc=asbc_send(g_cli,ASBC_FRAME,g_pkt,(uint32_t)(15+dn)); g_sent_by+=15+dn; inflight_push(); }
  (void)t0;
  return rc; }

/* once a second: frames + bytes sent and the average encode (diff+convert+pack) time, for the client's status bar */
static void stats_send(void){
  static long long last; long long t=us_now(); if(!last){ last=t; return; } if(t-last<1000000) return; last=t;
  if(g_cli>=0 && (g_ff&ASBC_FF_STATS)){
    unsigned char b[14]; unsigned long by=(unsigned long)g_sent_by, enc=g_enc_n?(unsigned long)(g_enc_us/g_enc_n):0;
    asbc_put_u16(b,(unsigned)g_sent_fr); asbc_put_u16(b+2,(unsigned)(by&0xFFFF)); asbc_put_u16(b+4,(unsigned)(by>>16));
    asbc_put_u16(b+6,(unsigned)(enc&0xFFFF)); asbc_put_u16(b+8,(unsigned)(enc>>16));
    /* 0.81: video latency (capture->ACK, ms) and the host's own audio delay (Chrome->pulse->parec->40 ms packets);
       older clients read only the first 10 bytes */
    asbc_put_u16(b+10,(unsigned)(g_vlat_ms>65535?65535:g_vlat_ms)); asbc_put_u16(b+12,(unsigned)AU_HOST_MS);
    asbc_send(g_cli,ASBC_STATS,b,14);
    { static int k; if(g_vlat_ms && ++k%30==0) fprintf(stderr,"asb-chromed: video latency %d ms (capture to on-screen)\n",g_vlat_ms); } }
  g_sent_by=0; g_sent_fr=0; g_enc_us=0; g_enc_n=0; }
