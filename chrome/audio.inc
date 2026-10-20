/* ---- 0.74 streamed audio: the browser plays into a private PulseAudio/PipeWire null sink; we capture its
   monitor with parec (or ffmpeg -f pulse), 22050 Hz s16 stereo, and send ASBC_AUDIO packets of 40 ms.
   ASB_AUDIO=0 turns it off. Works headless (no X): only a Pulse server is needed; if none answers and the
   pulseaudio binary exists, we start a private one. ---- */
static char g_au_sink[64]; static int g_au_mod=-1, g_au_fd=-1, g_au_caps, g_au_on=1, g_au_ok;
static pid_t g_au_pid;
static unsigned char g_au_buf[8192]; static int g_au_n;
static int g_au_pk, g_au_bytes, g_au_lastpk;   /* packets/bytes this second, for logs */
static int g_au_ipred[2], g_au_iidx[2];
static int au_sh(const char *cmd,char *out,int cap){
  FILE *f=popen(cmd,"r"); if(!f) return -1; int n=0;
  if(out&&cap>0){ n=(int)fread(out,1,(size_t)cap-1,f); if(n<0) n=0; out[n]=0; } else { char t[256]; while(fread(t,1,sizeof t,f)>0){} }
  return pclose(f); }
/* PATH lookup without system(): SIGCHLD is ignored in the host, so exit codes are not available */
static int au_have(const char *b){ const char *pa=getenv("PATH"); char d[600];
  if(!pa) pa="/usr/bin:/bin";
  while(*pa){ int n=(int)strcspn(pa,":"); snprintf(d,sizeof d,"%.*s/%s",n,pa,b); if(!access(d,X_OK)) return 1; pa+=n; if(*pa) pa++; }
  return 0; }
/* before the browser starts: make the sink, export PULSE_SINK so the browser plays into it */
static void audio_setup(void){
  const char *e=getenv("ASB_AUDIO"); if(e&&e[0]=='0'){ fprintf(stderr,"asb-chromed: audio off (ASB_AUDIO=0)\n"); return; }
  if(!au_have("pactl")){ fprintf(stderr,"asb-chromed: audio unavailable (no pactl; install pulseaudio-utils)\n"); return; }
  char info[2048];
  au_sh("pactl info 2>/dev/null",info,sizeof info);
  if(!strstr(info,"Server")){
    if(au_have("pulseaudio")){ au_sh("pulseaudio --start --exit-idle-time=-1 >/dev/null 2>&1",NULL,0); usleep(800000); }
    au_sh("pactl info 2>/dev/null",info,sizeof info);
    if(!strstr(info,"Server")){ fprintf(stderr,"asb-chromed: audio unavailable (no PulseAudio/PipeWire server)\n"); return; } }
  snprintf(g_au_sink,sizeof g_au_sink,"asb_%d",(int)getpid());
  char c[300], out[64];
  snprintf(c,sizeof c,"pactl load-module module-null-sink sink_name=%s sink_properties=device.description=AmiSurf rate=%d channels=2 2>/dev/null",g_au_sink,ASBC_AU_RATE);
  au_sh(c,out,sizeof out);
  if(out[0]<'0'||out[0]>'9'){ fprintf(stderr,"asb-chromed: audio: could not create null sink\n"); return; }
  g_au_mod=atoi(out); setenv("PULSE_SINK",g_au_sink,1);
  g_au_ok=1; fprintf(stderr,"asb-chromed: audio sink %s (module %d)\n",g_au_sink,g_au_mod); }
static void audio_stop_cap(void){
  if(g_au_fd>=0){ close(g_au_fd); g_au_fd=-1; }
  if(g_au_pid>0){ kill(g_au_pid,SIGTERM); g_au_pid=0; }   /* our own child, by PID */
  g_au_n=0; }
static void audio_cleanup(void){
  audio_stop_cap();
  if(g_au_mod>=0){ char c[80]; snprintf(c,sizeof c,"pactl unload-module %d >/dev/null 2>&1",g_au_mod); au_sh(c,NULL,0); g_au_mod=-1; } }
static void audio_start_cap(void){
  if(!g_au_ok || g_au_fd>=0 || !g_au_caps || !g_au_on || g_cli<0) return;
  int pp[2]; if(pipe(pp)) return;
  char dev[96], rate[16]; snprintf(dev,sizeof dev,"%s.monitor",g_au_sink); snprintf(rate,sizeof rate,"%d",ASBC_AU_RATE);
  int parec=au_have("parec");
  pid_t p=fork(); if(p<0){ close(pp[0]); close(pp[1]); return; }
  if(p==0){ int dn=open("/dev/null",O_RDWR); dup2(pp[1],1); if(dn>=0){ dup2(dn,0); dup2(dn,2); } close(pp[0]);
    if(parec) execlp("parec","parec","-d",dev,"--raw","--format=s16le","--rate",rate,"--channels=2","--latency-msec=20",(char*)0);
    else execlp("ffmpeg","ffmpeg","-loglevel","quiet","-f","pulse","-fragment_size","1764","-i",dev,"-ac","2","-ar",rate,"-f","s16le","-",(char*)0);
    _exit(127); }
  close(pp[1]); g_au_fd=pp[0]; fcntl(g_au_fd,F_SETFL,O_NONBLOCK); g_au_pid=p; g_au_n=0;
  g_au_ipred[0]=g_au_ipred[1]=0; g_au_iidx[0]=g_au_iidx[1]=0;
  fprintf(stderr,"asb-chromed: audio capture %s pid %d\n",parec?"parec":"ffmpeg",(int)p); }
static int au_ima_enc(int s,int ch){
  int st=asbc_ima_step[g_au_iidx[ch]], d=s-g_au_ipred[ch], nib=0;
  if(d<0){ nib=8; d=-d; }
  if(d>=st){ nib|=4; d-=st; } if(d>=st>>1){ nib|=2; d-=st>>1; } if(d>=st>>2) nib|=1;
  asbc_ima_dec(nib,&g_au_ipred[ch],&g_au_iidx[ch]); return nib; }
#define AU_PKT 882   /* frames per packet: 40 ms */
static void audio_send_pkt(const short *s,int nf){
  static unsigned char o[12+4*AU_PKT+8]; int fmt, len=0, i, loud=0;
  for(i=0;i<nf*2;i++){ int v=s[i]; if(v>96||v<-96){ loud=1; break; } }
  if(!loud) fmt=ASBC_AU_SIL;
  else if(g_au_caps&ASBC_AF_S16) fmt=ASBC_AU_S16;
  else if((g_au_caps&ASBC_AF_ADPCM) && getenv("ASB_AUDIO_ADPCM")) fmt=ASBC_AU_ADPCM;
  else if(g_au_caps&ASBC_AF_S8) fmt=ASBC_AU_S8;
  else if(g_au_caps&ASBC_AF_ADPCM) fmt=ASBC_AU_ADPCM;
  else return;
  unsigned char *d=o+10;
  if(fmt==ASBC_AU_S8) for(i=0;i<nf*2;i++) d[len++]=(unsigned char)(signed char)(s[i]>>8);
  else if(fmt==ASBC_AU_S16) for(i=0;i<nf*2;i++){ d[len++]=(unsigned char)(s[i]>>8); d[len++]=(unsigned char)s[i]; }
  else if(fmt==ASBC_AU_ADPCM){
    for(int c=0;c<2;c++){ d[len++]=(unsigned char)(g_au_ipred[c]>>8); d[len++]=(unsigned char)g_au_ipred[c]; d[len++]=(unsigned char)g_au_iidx[c]; d[len++]=0; }
    for(i=0;i<nf;i++){ int l=au_ima_enc(s[2*i],0), r=au_ima_enc(s[2*i+1],1); d[len++]=(unsigned char)(l|(r<<4)); } }
  if(fmt!=ASBC_AU_ADPCM){ g_au_ipred[0]=s[2*nf-2]; g_au_ipred[1]=s[2*nf-1]; }
  o[0]=(unsigned char)fmt; o[1]=2; asbc_put_u16(o+2,ASBC_AU_RATE);
  unsigned long ms=(unsigned long)now_ms(); o[4]=(unsigned char)ms; o[5]=(unsigned char)(ms>>8); o[6]=(unsigned char)(ms>>16); o[7]=(unsigned char)(ms>>24);
  asbc_put_u16(o+8,(unsigned)nf);
  asbc_send(g_cli,ASBC_AUDIO,o,(uint32_t)(10+len)); g_au_pk++; g_au_bytes+=10+len; }
/* main loop: read what the capture produced, send full 40 ms packets */
static void audio_pump(void){
  if(g_au_fd<0) return;
  for(;;){ int k=(int)read(g_au_fd,g_au_buf+g_au_n,sizeof g_au_buf-(size_t)g_au_n);
    if(k==0){ fprintf(stderr,"asb-chromed: audio capture ended\n"); audio_stop_cap(); return; }
    if(k<0) break; g_au_n+=k;
    while(g_au_n>=AU_PKT*4){ short s[AU_PKT*2]; for(int i=0;i<AU_PKT*2;i++) s[i]=(short)(g_au_buf[2*i]|(g_au_buf[2*i+1]<<8));
      if(g_cli>=0) audio_send_pkt(s,AU_PKT);
      memmove(g_au_buf,g_au_buf+AU_PKT*4,(size_t)g_au_n-AU_PKT*4); g_au_n-=AU_PKT*4; }
    if(g_au_n>=(int)sizeof g_au_buf) g_au_n=0; }
  { static time_t last; time_t t=time(NULL); if(t!=last){ last=t; g_au_lastpk=g_au_pk; if(getenv("ASB_DEBUG")&&g_au_pk) fprintf(stderr,"asb-chromed: audio %d pk/s %d B/s\n",g_au_pk,g_au_bytes); g_au_pk=g_au_bytes=0; } } }
