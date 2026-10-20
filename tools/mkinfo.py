#!/usr/bin/env python3
# Minimal OS1.3-style .info writer (2-plane planar image), no deps.
import struct,sys
def image(w,h):
    rows=[]
    for y in range(h):
        r=[]
        for x in range(w):
            edge = x<2 or x>=w-2 or y<1 or y>=h-1
            globe = (x-w/2)**2/(w/3)**2+(y-h/2)**2/(h/3)**2<1
            r.append(1 if edge else (3 if globe else 0) if not (globe and (x+y)%6<2) else 2)
        rows.append(r)
    wpr=(w+15)//16; data=b''
    for p in range(2):
        for r in rows:
            bits=0
            for x in range(wpr*16):
                bits=(bits<<1)|((r[x]>>p)&1 if x<w else 0)
            data+=bits.to_bytes(wpr*2,'big')
    return struct.pack('>hhhhhIBBI',0,0,w,h,2,1,3,0,0)+data
def info(kind,deftool=None,stack=65536,tooltypes=()):
    w,h=40,22
    t={'tool':3,'project':4,'drawer':2}[kind]
    gad=struct.pack('>IhhhhHHHIIIIIHI',0,0,0,w,h,4,3,1,1,0,0,0,0,0,0)
    d=struct.pack('>HH',0xE310,1)+gad+struct.pack('>BBIIIIIII',t,0,
        1 if deftool else 0,1 if tooltypes else 0,0x80000000,0x80000000,
        1 if kind=='drawer' else 0,0,stack)
    if kind=='drawer':
        d+=struct.pack('>hhhhBBIIIIIIIhhhhH',50,50,400,200,255,255,0,0x240027f,0,0,0,0,0,90,40,-1,-1,1)+struct.pack('>ii',0,0)
    d+=image(w,h)
    s=lambda x:struct.pack('>I',len(x)+1)+x.encode('latin-1')+b'\0'
    if deftool: d+=s(deftool)
    if tooltypes:
        d+=struct.pack('>I',(len(tooltypes)+1)*4)+b''.join(s(x) for x in tooltypes)
    return d
if __name__=='__main__':
    out,kind=sys.argv[1],sys.argv[2]
    open(out,'wb').write(info(kind,sys.argv[3] if len(sys.argv)>3 and sys.argv[3] else None,tooltypes=tuple(sys.argv[4:])))
