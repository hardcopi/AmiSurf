# temporary benchmark instrumentation for ui/asb_gfx_chrome.c (never committed)
import sys,re
p=sys.argv[1]; s=open(p).read()
hdr='''
#include <devices/timer.h>
#include <proto/timer.h>
struct Device *TimerBase; static struct timerequest b_tr; static int b_ok;
static unsigned long long b_us(void){ if(!b_ok){ if(OpenDevice((STRPTR)"timer.device",UNIT_ECLOCK,(struct IORequest*)&b_tr,0)==0){ TimerBase=b_tr.tr_node.io_Device; b_ok=1; } else return 0; }
  struct EClockVal e; ULONG f=ReadEClock(&e); return (((unsigned long long)e.ev_hi<<32)|e.ev_lo)*1000000ULL/f; }
static unsigned long long b_nav, b_last, b_rep; static unsigned long b_fr, b_bytes, b_acc;
'''
s=s.replace('static void send_nav(const char *u);',hdr+'static void send_nav(const char *u);',1)
s=re.sub(r'(static void send_nav\(const char \*u\)\{)',r'\1 b_nav=b_us(); b_last=0;',s,count=1)
s=s.replace('static void apply_frame(unsigned char *p,int n){','static void apply_frame_x(unsigned char *p,int n);\nstatic void apply_frame(unsigned char *p,int n){ unsigned long long t=b_us(); apply_frame_x(p,n); unsigned long long e=b_us(); b_acc+=(unsigned long)(e-t); b_fr++; b_bytes+=(unsigned long)n; b_last=e; }\nstatic void apply_frame_x(unsigned char *p,int n){',1)
a='    if(g_fd>=0) WaitSelect(nf, &rf, NULL, NULL, &tv, &sigs); else Delay(3);'
assert a in s
s=s.replace(a,a+'''
    { unsigned long long t=b_us(); if(g_fd>=0 && t-b_rep>=1000000ULL){ unsigned long dt=(unsigned long)(t-b_rep); b_rep=t;
        send_cmd("stat fr=%lu bytes=%lu dtms=%lu cms=%lu ready=%ld",b_fr,b_bytes,dt/1000,b_fr?b_acc/b_fr/100:0,b_last&&b_nav?(long)((b_last-b_nav)/1000):-1);
        b_fr=0; b_bytes=0; b_acc=0; } }''',1)
open(p,'w').write(s)
s=open(p).read()
if 'static void apply_scroll(unsigned char *p){' in s:
    s=s.replace('static void apply_scroll(unsigned char *p){','static void apply_scroll_x(unsigned char *p);\nstatic void apply_scroll(unsigned char *p){ unsigned long long t=b_us(); apply_scroll_x(p); unsigned long long e=b_us(); b_acc+=(unsigned long)(e-t); b_fr++; b_bytes+=10; b_last=e; }\nstatic void apply_scroll_x(unsigned char *p){',1)
    open(p,'w').write(s)
# 0.71: the MUI front end has its own main loop
import os
m=os.path.join(os.path.dirname(p),'asb_mui.inc')
if os.path.exists(m):
    s=open(m).read()
    a='      int ns=WaitSelect(g_fd+1,&rf,NULL,NULL,&tv,&z);'
    if a in s and 'b_rep' not in s:
        s=s.replace(a,a+'''
      { unsigned long long t=b_us(); if(g_fd>=0 && t-b_rep>=1000000ULL){ unsigned long dt=(unsigned long)(t-b_rep); b_rep=t;
          send_cmd("stat fr=%lu bytes=%lu dtms=%lu cms=%lu ready=%ld",b_fr,b_bytes,dt/1000,b_fr?b_acc/b_fr/100:0,b_last&&b_nav?(long)((b_last-b_nav)/1000):-1);
          b_fr=0; b_bytes=0; b_acc=0; } }''',1)
        open(m,'w').write(s)
