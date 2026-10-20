#!/usr/bin/env python3
"""Scriptable ASBC test client: python3 tools/asbc_probe.py PORT 'nav https://x' 'wait 3' 'cmd zoom +' 'find foo' ...
Prints STATUS/META/CTX/SUGG/ZOOM/FINDRES/TABS messages; FRAME is counted. Token from ASB_TOKEN."""
import socket, struct, sys, time, os, select
ops={'HELLO':1,'NAV':2,'BACK':3,'FWD':4,'RELOAD':5,'FIND':13,'TABNEW':15,'TABCLS':16,'TABSEL':17,'CMD':18,'MOUSE':7}
names={32:'FRAME',33:'META',35:'STATUS',36:'CLIP',37:'TABS',38:'CTX',39:'SUGG',40:'ZOOM',41:'FINDRES',34:'CURSOR'}
port=int(sys.argv[1]); tok=os.environ.get('ASB_TOKEN','').encode()
s=socket.create_connection(('127.0.0.1',port))
s.sendall(b'ASBC'+struct.pack('<HB',2,len(tok))+tok)
r=s.recv(7); assert r[:4]==b'ASBC' and r[6]==0, r
def send(op,p=b''): s.sendall(struct.pack('<IB',len(p)+1,op)+p)
buf=b''; frames=0; fbytes=0
def pump(t):
    global buf,frames,fbytes
    end=time.time()+t
    while True:
        left=end-time.time()
        if left<=0: break
        if select.select([s],[],[],left)[0]:
            d=s.recv(1<<20)
            if not d: print('closed'); sys.exit(1)
            buf+=d
        while len(buf)>=5:
            n,op=struct.unpack('<IB',buf[:5])
            if len(buf)<4+n: break
            p=buf[5:4+n]; buf=buf[4+n:]
            nm=names.get(op,str(op))
            if op in (32,43) and os.environ.get('PROBE_FMT'): send(20,b'')
            if op==43: frames+=1; fbytes+=len(p); print('FRAME %.3f %d'%(time.time(),len(p))) if os.environ.get('ASB_FRAMES') else None; continue
            if op==32:
                frames+=1; fbytes+=len(p)
                if os.environ.get('ASB_FRAMES'): print('FRAME %.3f %d'%(time.time(),len(p)))
                continue
            if op==33:
                ul=struct.unpack('<H',p[1:3])[0]; u=p[3:3+ul].decode('utf8','replace'); tl=struct.unpack('<H',p[3+ul:5+ul])[0]
                print('META',p[0],u,'|',p[5+ul:5+ul+tl].decode('utf8','replace')); continue
            if op in (40,): print(nm,struct.unpack('<H',p)[0]); continue
            if op==41: print('%s %d/%d'%((nm,)+struct.unpack('<HH',p))); continue
            if op==37:
                n2=p[0]; o=2; t=[]
                for i in range(n2):
                    l=struct.unpack('<H',p[o:o+2])[0]; t.append(p[o+2:o+2+l].decode('utf8','replace')); o+=2+l
                print('TABS active',p[1],t); continue
            print(nm,repr(p.decode('utf8','replace'))[:300])
send(1,struct.pack('<HH',1400,770)+(bytes([{'chrome':1,'firefox':2}[os.environ['PROBE_BROWSER']]]) if os.environ.get('PROBE_BROWSER') else b''))
if os.environ.get('PROBE_FMT'): send(19,bytes([int(os.environ['PROBE_FMT']),1|2|4,0,0]))
pump(1)
for a in sys.argv[2:]:
    v,_,rest=a.partition(' ')
    if v=='wait': pump(float(rest)); continue
    if v=='mark': print('MARK %.3f %s'%(time.time(),rest)); continue
    if v=='mouse':
        x,y,b,t=map(int,rest.split()); send(7,struct.pack('<HHBB',x,y,b,t)); continue
    if v=='type':
        for ch in rest: b=ch.encode(); send(9,struct.pack('<BHBH',2,0,0,len(b))+b)
        continue
    if v=='key':
        send(9,struct.pack('<BHBH',0,int(rest),0,0)); send(9,struct.pack('<BHBH',1,int(rest),0,0)); continue
    if v=='sel': send(17,bytes([int(rest)])); continue
    if v=='close': send(16,bytes([int(rest)])); continue
    print('>>',a)
    send(ops[v.upper()],rest.encode())
pump(1.5)
print('frames',frames,'bytes',fbytes)
