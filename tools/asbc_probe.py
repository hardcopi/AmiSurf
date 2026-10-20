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
aud={}; apk=0; ats=[]; pcm=bytearray()
acaps=int(os.environ.get('ASB_AUDIOCAPS','0'))
def pump(t):
    global buf,frames,fbytes,apk
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
            if op==32:
                frames+=1; fbytes+=len(p)
                if os.environ.get('ASB_FRAMES'): print('FRAME',len(p),struct.unpack('<HHHHHH',p[:12]))
                continue
            if op==44:
                fmt=p[0]; nf=p[8]|(p[9]<<8)
                ts=p[4]|(p[5]<<8)|(p[6]<<16)|(p[7]<<24)
                aud[fmt]=aud.get(fmt,0)+1; apk+=1; ats.append(time.time())
                d=p[10:]
                if fmt==1: pcm.extend(struct.pack('<%dh'%len(d),*[((b^128)-128)<<8 for b in d]))
                elif fmt==3: pcm.extend(struct.pack('<%dh'%(len(d)//2),*struct.unpack('>%dh'%(len(d)//2),d)))
                else: pcm.extend(b'\0'*(nf*4))
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
send(1,struct.pack('<HHBB',1024,700,0,acaps) if acaps else struct.pack('<HH',1024,700)); pump(1)
for a in sys.argv[2:]:
    v,_,rest=a.partition(' ')
    if v=='wait': pump(float(rest)); continue
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
if acaps:
    span=(ats[-1]-ats[0]) if len(ats)>1 else 0
    print('audio packets',apk,'by fmt',aud,'rate %.1f pk/s'%((len(ats)-1)/span if span else 0))
    gaps=[b-a for a,b in zip(ats,ats[1:])]; print('max gap %.0f ms'%(max(gaps)*1000 if gaps else 0))
    if os.environ.get('ASB_WAV'):
        import wave; w=wave.open(os.environ['ASB_WAV'],'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(22050); w.writeframes(bytes(pcm)); w.close()
