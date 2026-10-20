/* ---- 0.74 streamed audio: ASBC_AUDIO packets -> jitter ring (s16 stereo, fast RAM) -> ahi.device (if present)
   or Paula via audio.device, double-buffered. SOUND=ON|OFF|ADPCM tooltype; Amiga-M / menu mutes. ---- */
#include <devices/audio.h>
#include <graphics/gfxbase.h>
/* ahi.device essentials (no AHI SDK in the toolchain) */
struct AHIRequest { struct IOStdReq ahir_Std; UWORD ahir_Version; UWORD ahir_Pad1; ULONG ahir_Private[2]; ULONG ahir_Type;
  ULONG ahir_Frequency; LONG ahir_Volume; LONG ahir_Position; struct AHIRequest *ahir_Link; };
#define AHIST_S8S 2
#define AHIST_S16S 3
#define AU_RING 32768          /* frames, ~1.5 s */
#define AU_CHUNK 2048          /* frames per device buffer, ~93 ms */
#define AU_PRIME 5512          /* ~250 ms jitter buffer before (re)starting */
#define AU_MAXLAT 11025        /* ~500 ms: beyond this we drop old audio to stay near live (and near the video) */
static int g_sound=1, g_sound_adpcm, g_mute, g_au_dev;   /* g_au_dev 0 none, 1 Paula, 2 AHI 8-bit, 3 AHI 16-bit */
static short *g_au_ring; static unsigned long g_au_w, g_au_r; static int g_au_priming=1;
static unsigned long g_au_pk, g_au_und, g_au_drop, g_au_lastfmt; static unsigned long g_au_pks, g_au_unds;   /* stats */
static struct MsgPort *g_au_port;
/* Paula */
static struct IOAudio *g_pa_alloc, *g_pa_io[2][2]; static BYTE *g_pa_buf[2][2]; static int g_pa_busy[2][2];
static UBYTE g_pa_lmask, g_pa_rmask;
/* AHI */
static struct AHIRequest *g_ahi_io[2], *g_ahi_last; static void *g_ahi_buf[2]; static int g_ahi_busy[2], g_ahi_open;
static unsigned long au_fill(void){ return g_au_w-g_au_r; }
/* ---- 0.81 A/V sync ----
   0.80 kept 250-500 ms in the ring plus ~140 ms in the device: sound ran 0.4-0.7 s behind the picture (beta report, 0.80), and
   with Paula's rounded period the ring drained slowly until an underrun re-primed it (drift, then a gap).
   Now the ring is steered to a target that makes the audio delay equal the video delay the host measures
   (capture -> frame on screen, ASBC_STATS): target = video - host audio - device queue, at least AU_MINRING.
   Small errors are corrected by dropping or repeating one sample in 50-100 (inaudible), large ones (> 300 ms) at once. */
#define AU_MINRING 2205        /* 100 ms: covers main-loop stalls while a big frame is drawn */
#define AU_DEVQ_MS (AU_CHUNK*3/2*1000/ASBC_AU_RATE)   /* average audio queued in the device (one playing, one waiting) */
static int g_vlat, g_hlat=70;   /* from ASBC_STATS (0.81 hosts); 0 = unknown: keep the old 250 ms buffer */
/* ---- 0.84: what happens after the device ----
   0.81-0.83 counted ring + device queue + host capture, calibrated in FS-UAE (whose own output buffer is ~40 ms).
   Amiberry pushes Paula/AHI output through SDL in 2-3 buffers of sound_max_buff/4 frames (default 16384 -> 4096
   frames, ~93 ms each) plus the SDL device buffer: ~250-350 ms more, so the sound came out ~300 ms after the picture
   (Rich, AmiKit/Amiberry, 0.83). No Amiga API sees that buffer, so:
   - the device queue is measured: the time each buffer finished playing (when the reply is noticed) tells how much
     is still queued in the device, instead of assuming 1.5 buffers;
   - g_av_outlat = latency after the device: 0 on a real Amiga and in other UAEs (FS-UAE's own output buffer is small: measured within ~10 ms),
     AV_AMIBERRY_MS when Amiberry is detected (uaelib 96 answers, or C:host-run exists);
   - g_avsync_user = the user's correction (AVSYNC=<ms> tooltype or the A/V sync menu, saved in the prefs):
     + = sound comes out later than we think -> picture held back more, less audio buffered. */
#define AV_AMIBERRY_MS 300
static int g_av_outlat=-1, g_av_emu, g_avsync_user;   /* g_av_emu 0 real/unknown, 1 UAE, 2 Amiberry */
static char g_av_emuname[64];
static unsigned long long asb_us(void);
static unsigned long long g_au_tdone; static long g_devq16=-1;   /* last buffer reply (us); device queue EMA, ms*16 */
#define AU_CHUNK_MS (AU_CHUNK*1000/ASBC_AU_RATE)
static int au_devq_ms(void){ return g_devq16<0?AU_DEVQ_MS:(int)(g_devq16/16); }
static void au_devq_sample(void){   /* two buffers queued since the last reply: the playing one started then */
  if(!g_au_tdone) return;
  long q=2L*AU_CHUNK_MS-(long)((asb_us()-g_au_tdone)/1000); if(q<0) q=0; if(q>2L*AU_CHUNK_MS) q=2L*AU_CHUNK_MS;
  g_devq16=g_devq16<0?q*16:g_devq16+q-g_devq16/16; }
static int av_out_ms(void){ return (g_av_outlat>0?g_av_outlat:0)+g_avsync_user; }
static ULONG uae_getfunc(struct Library *base,const char *name){   /* uae.resource LVO -6: A0 = trap name */
  register ULONG d0 __asm("d0"); register struct Library *a6 __asm("a6")=base; register const char *a0 __asm("a0")=name;
  __asm volatile("jsr -6(%%a6)":"=r"(d0):"r"(a6),"r"(a0):"d1","a1","cc","memory"); return d0; }
static void av_detect(void){
  if(g_av_outlat>=0) return; g_av_outlat=0; snprintf(g_av_emuname,sizeof g_av_emuname,"no emulator found");
  struct Library *r=(struct Library*)OpenResource((STRPTR)"uae.resource");
  if(r){ UWORD *v=(UWORD*)((UBYTE*)r+sizeof(struct Library)); ULONG plat=0, f=uae_getfunc(r,"uaelib_demux");
    if(f) plat=((ULONG(*)(ULONG,ULONG,ULONG))f)(96,0,0);   /* Amiberry: host platform 1-3; other UAEs: 0 */
    BPTR l=Lock((STRPTR)"C:host-run",ACCESS_READ); if(l) UnLock(l);
    g_av_emu=(plat>=1&&plat<=3)||l?2:1; if(g_av_emu==2) g_av_outlat=AV_AMIBERRY_MS;
    snprintf(g_av_emuname,sizeof g_av_emuname,"%s (uae.resource %u.%u.%u%s%s)",g_av_emu==2?"Amiberry":"UAE",v[0],v[1],v[2],
      plat>=1&&plat<=3?", uaelib 96":"",l?", C:host-run":""); }
  asb_log("av: %s, sound output latency after the device %d ms, AVSYNC %+d ms",g_av_emuname,g_av_outlat,g_avsync_user); }
static unsigned long g_au_loud_pk, g_au_pk_total, g_au_target=AU_PRIME, g_au_adj_n, g_au_jump; static int g_au_adj, g_au_ph;
static void au_retarget(void){
  long t=AU_PRIME;
#ifdef ASB_AV_OLD   /* test build: 0.80 buffering (fixed 250 ms prime, drop back at 500 ms, no steering) */
  g_au_target=AU_PRIME; g_vlat=0; g_au_adj=0; return;
#endif
  if(g_vlat>0){ long ms=(long)g_vlat-g_hlat-au_devq_ms()-av_out_ms(); t=ms*ASBC_AU_RATE/1000; if(t<AU_MINRING) t=AU_MINRING; if(t>ASBC_AU_RATE) t=ASBC_AU_RATE; }
  g_au_target=(unsigned long)t; }
static long au_err(void){ return (long)au_fill()-(long)g_au_target; }   /* frames: >0 audio later than the picture */
static int au_lat_ms(void){ return (int)(au_fill()*1000/ASBC_AU_RATE)+au_devq_ms()+g_hlat+av_out_ms(); }
static void au_push(int l,int r){
  if(au_fill()>=AU_RING){ g_au_r++; g_au_drop++; }
  unsigned long i=(g_au_w&(AU_RING-1))*2; g_au_ring[i]=(short)l; g_au_ring[i+1]=(short)r; g_au_w++; }
/* next frame for a device buffer: silence while priming or on underrun */
#ifdef ASB_AVPROBE
/* test build: time the white box hit the screen (0) and the beep reached the speaker (1, estimated: taken now from the
   ring into a device buffer that starts once the one playing ends, ~1 chunk later); log the offset */
static ULONG g_avp[2]; static int g_avp_ok[2];
static void avprobe_mark(int k){ ULONG s,u; CurrentTime(&s,&u); g_avp[k]=s*1000UL+u/1000UL+(k?AU_CHUNK*1000/ASBC_AU_RATE:0); g_avp_ok[k]=1;
  if(g_avp_ok[0]&&g_avp_ok[1]){ long d=(long)(g_avp[1]-g_avp[0]); if(d>-900&&d<900) asb_log("avprobe: sound %+ld ms after picture",d); g_avp_ok[0]=g_avp_ok[1]=0; } }
static int g_avp_loud;
#endif
static void au_take(int *l,int *r){
  if(g_au_priming){ if(au_fill()>=g_au_target) g_au_priming=0; else { *l=*r=0; return; } }
  if(!au_fill()){ g_au_und++; g_au_priming=1; *l=*r=0; return; }
  unsigned long i=(g_au_r&(AU_RING-1))*2; *l=g_au_ring[i]; *r=g_au_ring[i+1]; g_au_r++;
#ifdef ASB_AVPROBE
  { int a=*l<0?-*l:*l; if(a>6000){ if(!g_avp_loud) avprobe_mark(1); g_avp_loud=2205; } else if(g_avp_loud) g_avp_loud--; }
#endif
  }
static void au_fill_buf(int k){
  int i,l,r;
  if(g_au_dev==1){ BYTE *a=g_pa_buf[k][0], *b=g_pa_buf[k][1]; for(i=0;i<AU_CHUNK;i++){ au_take(&l,&r); a[i]=(BYTE)(l>>8); b[i]=(BYTE)(r>>8); } }
  else if(g_au_dev==3){ WORD *o=(WORD*)g_ahi_buf[k]; for(i=0;i<AU_CHUNK;i++){ au_take(&l,&r); o[2*i]=(WORD)l; o[2*i+1]=(WORD)r; } }
  else { BYTE *o=(BYTE*)g_ahi_buf[k]; for(i=0;i<AU_CHUNK;i++){ au_take(&l,&r); o[2*i]=(BYTE)(l>>8); o[2*i+1]=(BYTE)(r>>8); } } }
static void pa_send(int k,int c){
  struct IOAudio *io=g_pa_io[k][c];
  io->ioa_Request.io_Command=CMD_WRITE; io->ioa_Request.io_Flags=ADIOF_PERVOL;
  io->ioa_Request.io_Unit=(struct Unit*)(ULONG)(c?g_pa_rmask:g_pa_lmask);
  io->ioa_Data=(UBYTE*)g_pa_buf[k][c]; io->ioa_Length=AU_CHUNK;
  io->ioa_Period=(UWORD)(((GfxBase->DisplayFlags&PAL)?3546895UL:3579545UL)/ASBC_AU_RATE); io->ioa_Volume=64; io->ioa_Cycles=1;
  BeginIO((struct IORequest*)io); g_pa_busy[k][c]=1; }
static void ahi_send(int k){
  struct AHIRequest *io=g_ahi_io[k];
  io->ahir_Std.io_Command=CMD_WRITE; io->ahir_Std.io_Data=g_ahi_buf[k]; io->ahir_Std.io_Length=AU_CHUNK*(g_au_dev==3?4:2);
  io->ahir_Std.io_Offset=0; io->ahir_Type=g_au_dev==3?AHIST_S16S:AHIST_S8S; io->ahir_Frequency=ASBC_AU_RATE;
  io->ahir_Volume=0x10000; io->ahir_Position=0x8000; io->ahir_Link=g_ahi_last;
  SendIO((struct IORequest*)io); g_ahi_last=io; g_ahi_busy[k]=1; }
static void audio_close(void){
  int k,c;
  if(g_au_dev==1){
    for(k=0;k<2;k++) for(c=0;c<2;c++) if(g_pa_busy[k][c]){ AbortIO((struct IORequest*)g_pa_io[k][c]); WaitIO((struct IORequest*)g_pa_io[k][c]); g_pa_busy[k][c]=0; }
    if(g_pa_alloc) CloseDevice((struct IORequest*)g_pa_alloc); }
  if(g_au_dev>=2){
    for(k=0;k<2;k++) if(g_ahi_busy[k]){ AbortIO((struct IORequest*)g_ahi_io[k]); WaitIO((struct IORequest*)g_ahi_io[k]); g_ahi_busy[k]=0; }
    if(g_ahi_open) CloseDevice((struct IORequest*)g_ahi_io[0]); g_ahi_open=0; }
  for(k=0;k<2;k++){ for(c=0;c<2;c++){ if(g_pa_buf[k][c]) FreeMem(g_pa_buf[k][c],AU_CHUNK); g_pa_buf[k][c]=NULL; if(g_pa_io[k][c]) FreeMem(g_pa_io[k][c],sizeof(struct IOAudio)); g_pa_io[k][c]=NULL; }
    if(g_ahi_buf[k]) FreeMem(g_ahi_buf[k],AU_CHUNK*4); g_ahi_buf[k]=NULL;
    if(g_ahi_io[k]) DeleteIORequest((struct IORequest*)g_ahi_io[k]); g_ahi_io[k]=NULL; }
  if(g_pa_alloc){ DeleteIORequest((struct IORequest*)g_pa_alloc); g_pa_alloc=NULL; }
  if(g_au_port){ DeleteMsgPort(g_au_port); g_au_port=NULL; }
  if(g_au_ring){ FreeVec(g_au_ring); g_au_ring=NULL; }
  g_au_dev=0; g_ahi_last=NULL; }
static int ahi_open(void){
  int k;
  for(k=0;k<2;k++){ g_ahi_io[k]=(struct AHIRequest*)CreateIORequest(g_au_port,sizeof(struct AHIRequest)); if(!g_ahi_io[k]) return 0; }
  g_ahi_io[0]->ahir_Version=4;
  if(OpenDevice((STRPTR)"ahi.device",0,(struct IORequest*)g_ahi_io[0],0)) return 0;
  g_ahi_open=1; *g_ahi_io[1]=*g_ahi_io[0];
#ifdef ASP_020
  g_au_dev=2;
#else
  g_au_dev=3;   /* 040: 16-bit through AHI */
#endif
  for(k=0;k<2;k++){ g_ahi_buf[k]=AllocMem(AU_CHUNK*4,MEMF_PUBLIC|MEMF_CLEAR); if(!g_ahi_buf[k]) return 0; }
  return 1; }
static int paula_open(void){
  static UBYTE map[]={3,5,10,12}; int k,c;
  g_pa_alloc=(struct IOAudio*)CreateIORequest(g_au_port,sizeof(struct IOAudio)); if(!g_pa_alloc) return 0;
  g_pa_alloc->ioa_Request.io_Message.mn_Node.ln_Pri=0; g_pa_alloc->ioa_Data=map; g_pa_alloc->ioa_Length=sizeof map;
  if(OpenDevice((STRPTR)AUDIONAME,0,(struct IORequest*)g_pa_alloc,0)){ DeleteIORequest((struct IORequest*)g_pa_alloc); g_pa_alloc=NULL; return 0; }
  UBYTE m=(UBYTE)(ULONG)g_pa_alloc->ioa_Request.io_Unit; g_pa_lmask=m&9; g_pa_rmask=m&6;   /* Paula: 0,3 left; 1,2 right */
  g_pa_lmask&=(UBYTE)-(BYTE)g_pa_lmask; g_pa_rmask&=(UBYTE)-(BYTE)g_pa_rmask;   /* one channel each */
  g_au_dev=1;
  for(k=0;k<2;k++) for(c=0;c<2;c++){
    g_pa_io[k][c]=AllocMem(sizeof(struct IOAudio),MEMF_PUBLIC|MEMF_CLEAR); g_pa_buf[k][c]=AllocMem(AU_CHUNK,MEMF_CHIP|MEMF_CLEAR);
    if(!g_pa_io[k][c]||!g_pa_buf[k][c]) return 0;
    *g_pa_io[k][c]=*g_pa_alloc; }
  return 1; }
/* open on connect; returns the HELLO audio caps */
static int audio_open(void){
  if(!g_sound) return 0;
  if(g_au_dev) return g_sound_adpcm?ASBC_AF_ADPCM:(ASBC_AF_S8|ASBC_AF_ADPCM|(g_au_dev==3?ASBC_AF_S16:0));
  g_au_ring=AllocVec(AU_RING*4,MEMF_PUBLIC|MEMF_CLEAR); g_au_port=CreateMsgPort();
  if(!g_au_ring||!g_au_port){ audio_close(); return 0; }
  if(!ahi_open()){ audio_close(); g_au_ring=AllocVec(AU_RING*4,MEMF_PUBLIC|MEMF_CLEAR); g_au_port=CreateMsgPort();
    if(!g_au_ring||!g_au_port||!paula_open()){ audio_close(); return 0; } }
  g_au_w=g_au_r=0; g_au_priming=1; g_au_tdone=0; g_devq16=-1; av_detect();
  int k,c;
  if(g_au_dev==1){
    struct IOAudio *s=g_pa_io[0][0];   /* start both channels together: stop, queue, start */
    s->ioa_Request.io_Unit=(struct Unit*)(ULONG)(g_pa_lmask|g_pa_rmask); s->ioa_Request.io_Command=CMD_STOP; s->ioa_Request.io_Flags=IOF_QUICK; DoIO((struct IORequest*)s);
    for(k=0;k<2;k++){ au_fill_buf(k); for(c=0;c<2;c++) pa_send(k,c); }
    struct IOAudio st=*g_pa_alloc; st.ioa_Request.io_Unit=(struct Unit*)(ULONG)(g_pa_lmask|g_pa_rmask); st.ioa_Request.io_Command=CMD_START; st.ioa_Request.io_Flags=IOF_QUICK;
    DoIO((struct IORequest*)&st); }
  else for(k=0;k<2;k++){ au_fill_buf(k); ahi_send(k); }
  return audio_open(); }
static ULONG audio_sigmask(void){ return g_au_port?1UL<<g_au_port->mp_SigBit:0; }
/* refill whatever buffers the device has finished; called every loop pass and between network messages */
static void audio_service(void){
  if(!g_au_dev) return;
  while(GetMsg(g_au_port)){}
  int k;
  if(!g_au_priming && !g_mute) au_devq_sample();
  if(g_au_dev==1){
    for(k=0;k<2;k++) if(g_pa_busy[k][0]&&g_pa_busy[k][1]&&CheckIO((struct IORequest*)g_pa_io[k][0])&&CheckIO((struct IORequest*)g_pa_io[k][1])){
      WaitIO((struct IORequest*)g_pa_io[k][0]); WaitIO((struct IORequest*)g_pa_io[k][1]); g_pa_busy[k][0]=g_pa_busy[k][1]=0; g_au_tdone=asb_us();
      au_fill_buf(k); pa_send(k,0); pa_send(k,1); } }
  else for(k=0;k<2;k++) if(g_ahi_busy[k] && CheckIO((struct IORequest*)g_ahi_io[k])){
      WaitIO((struct IORequest*)g_ahi_io[k]); g_ahi_busy[k]=0; if(g_ahi_last==g_ahi_io[k]) g_ahi_last=NULL; g_au_tdone=asb_us();
      au_fill_buf(k); ahi_send(k); } }
static void au_push_adj(int l,int r){
  if(g_au_adj && ++g_au_ph>=(g_au_adj<0?-g_au_adj:g_au_adj)){ g_au_ph=0; g_au_adj_n++; if(g_au_adj<0) return; au_push(l,r); }
  au_push(l,r); }
/* one ASBC_AUDIO packet */
static void audio_packet(const unsigned char *p,int n){
  if(!g_au_dev || g_mute || n<10) return;
  int fmt=p[0], ch=p[1], nf=p[8]|(p[9]<<8), i; const unsigned char *d=p+10; n-=10;
  if(ch!=2) return;
  g_au_pk++; g_au_pk_total++; g_au_lastfmt=(unsigned long)fmt; if(fmt!=ASBC_AU_SIL) g_au_loud_pk=g_au_pk_total;
  au_retarget();
#ifndef ASB_AV_OLD
  { long e=au_err(), a=e<0?-e:e;   /* steer: 1 sample in 100 (|err| > 40 ms) or in 50 (> 100 ms); stop inside 10 ms */
    if(a>ASBC_AU_RATE/25) g_au_adj=(e>0?-1:1)*(a>ASBC_AU_RATE/10?50:100); else if(a<ASBC_AU_RATE/100) g_au_adj=0; }
#endif
  if(fmt==ASBC_AU_SIL) for(i=0;i<nf;i++) au_push_adj(0,0);
  else if(fmt==ASBC_AU_S8){ if(n<nf*2) nf=n/2; for(i=0;i<nf;i++) au_push_adj((signed char)d[2*i]<<8,(signed char)d[2*i+1]<<8); }
  else if(fmt==ASBC_AU_S16){ if(n<nf*4) nf=n/4; for(i=0;i<nf;i++) au_push_adj((short)((d[4*i]<<8)|d[4*i+1]),(short)((d[4*i+2]<<8)|d[4*i+3])); }
  else if(fmt==ASBC_AU_ADPCM && n>=8){
    int pl=(short)((d[0]<<8)|d[1]), il=d[2], pr=(short)((d[4]<<8)|d[5]), ir=d[6]; if(il>88) il=88; if(ir>88) ir=88;
    d+=8; n-=8; if(n<nf) nf=n;
    for(i=0;i<nf;i++){ int l=asbc_ima_dec(d[i]&15,&pl,&il), r=asbc_ima_dec(d[i]>>4,&pr,&ir); au_push_adj(l,r); } }
  /* far off (a burst from the network, a stall, the video latency changed a lot): jump to the target now */
  { long e=au_err(), lim=g_vlat>0?ASBC_AU_RATE*3/10:(long)(AU_MAXLAT-AU_PRIME);
    if(e>lim){ g_au_r+=(unsigned long)e; g_au_drop+=(unsigned long)e; g_au_jump++; }
    else if(g_vlat>0 && !g_au_priming && e< -lim){ long k=-e; if(k>ASBC_AU_RATE*4/10) k=ASBC_AU_RATE*4/10; while(k-->0) au_push(0,0); g_au_jump++; } }
  audio_service(); }
static void audio_mute(int m){
  g_mute=m; if(g_fd>=0){ unsigned char b=(unsigned char)!m; asbc_send(g_fd,ASBC_AUDIOCTL,&b,1); }
  if(m && g_au_ring){ g_au_r=g_au_w; g_au_priming=1; } }
/* status-bar text, once a second */
static void audio_stats(char *o,int cap){
  static const char *fn[]={"-","8","A","16"};
  if(!g_sound){ snprintf(o,cap,"  sound off"); return; }
  if(!g_au_dev){ snprintf(o,cap,"  no audio device"); return; }
  if(g_mute){ snprintf(o,cap,"  muted"); return; }
  snprintf(o,cap," snd %s%s %lu/s %lums u%lu",g_au_dev==1?"P":"AHI",fn[g_au_lastfmt&3],g_au_pks,au_fill()*1000/ASBC_AU_RATE,g_au_unds);
  if(g_vlat>0){ int l=(int)strlen(o); snprintf(o+l,cap-l," av %d/%d",g_vlat,au_lat_ms()); }
  if(av_out_ms()){ int l=(int)strlen(o); snprintf(o+l,cap-l," sync %+d",av_out_ms()); }
  }
/* every 30 s while sound plays: the numbers behind the A/V sync, for beta logs */
static void audio_log_tick(void){
  static int n; static unsigned long lastpk;
  if(!g_au_dev || g_mute || ++n<30) return; n=0;
  if(g_au_pk_total==lastpk) return; lastpk=g_au_pk_total;
  asb_log("av: video %d ms, audio %d ms (buffer %lu ms, target %lu ms, device %d ms, after device %d ms), adjust %lu, jumps %lu, underruns %lu, dropped %lu",
    g_vlat,au_lat_ms(),au_fill()*1000/ASBC_AU_RATE,g_au_target*1000/ASBC_AU_RATE,au_devq_ms(),av_out_ms(),g_au_adj_n,g_au_jump,g_au_und,g_au_drop); }
static int g_av_sent;   /* last avdelay sent on this connection */
/* AVSYNC tooltype: OFF = never hold the picture back; a number = g_avsync_user (+ sound later than we think: picture
   held back more; - picture earlier). Trim: 0.81 set -60 from a probe that timed the beep at Paula with a fixed
   device-queue guess; 0.84 measures the device queue and verified with the real sound output recorded (FS-UAE,
   test/av-probe.html + tools/avsync-measure.py): -60 left the sound ~65 ms late there, 0 is within ~+10 ms. */
#define AV_TRIM_DEF 0
static int g_avsync_off, g_avsync_trim=AV_TRIM_DEF;
static void avsync_set(int ms){ if(ms<-1000) ms=-1000; if(ms>1000) ms=1000; g_avsync_user=ms; }
/* once a second: when the picture would be ahead of the sound (fast link, sound still needs its buffers), ask the
   engine to hold the picture back by the difference; 0 when no sound plays, so typing and scrolling stay immediate */
static void audio_av_request(void){
  static unsigned long lastpk; int sent=g_av_sent;
  int want=0;
  int playing=g_au_dev && !g_mute && g_vlat>0 && g_au_loud_pk && g_au_pk_total-g_au_loud_pk<50 && g_au_pk_total!=lastpk;
  lastpk=g_au_pk_total;
#ifndef ASB_AV_OLD
  if(playing){ int la=(int)(g_au_target*1000/ASBC_AU_RATE)+au_devq_ms()+g_hlat+av_out_ms(); want=la-g_vlat+g_avsync_trim; if(g_avsync_off||want<40) want=0; if(want>1500) want=1500; want=want/20*20; }
#endif
  if(g_fd>=0 && (want==0?sent!=0:(want-sent>=40||sent-want>=40))){ g_av_sent=want; send_cmd("avdelay %d",want); asb_log("av: picture held back %d ms (video %d ms, audio %d ms incl. device %d + after %d)",want,g_vlat,(int)(g_au_target*1000/ASBC_AU_RATE)+au_devq_ms()+g_hlat+av_out_ms(),au_devq_ms(),av_out_ms()); } }
