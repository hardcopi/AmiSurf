#!/usr/bin/env python3
"""mkappicons.py: draw the AmiSurf app/drawer art in the AmiStudio Paint 1.0.61 style
(rounded gradient tile, hexagon emblem, two-letter mark; selected state with a yellow glow) and write
  dist/info/AmiSurf.info  and  dist/info/drawer.info   as DualPNG icons (two PNGs joined, like Paint),
  ui/icons/about.png (64x64) and ui/icons/splash.png (from assets/about-banner-512.png).
Needs Pillow. Run from the repo root."""
import io, math
from PIL import Image, ImageDraw, ImageFilter, ImageFont
S=4; N=64*S
def hexagon(d,cx,cy,r,**kw):
    pts=[(cx+r*math.cos(math.radians(60*i-90)),cy+r*math.sin(math.radians(60*i-90))) for i in range(6)]
    d.polygon(pts,**kw)
def globe(d,cx,cy,r,w,col):
    d.ellipse([cx-r,cy-r,cx+r,cy+r],outline=col,width=w)
    d.ellipse([cx-r*0.45,cy-r,cx+r*0.45,cy+r],outline=col,width=w)
    d.line([cx-r,cy,cx+r,cy],fill=col,width=w)
    d.line([cx,cy-r,cx,cy+r],fill=col,width=w)
def font(sz):
    for f in ["/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf","/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"]:
        try: return ImageFont.truetype(f,sz)
        except OSError: pass
    return ImageFont.load_default()
def tile(selected, drawer=False):
    im=Image.new('RGBA',(N,N),(0,0,0,0)); d=ImageDraw.Draw(im)
    m=6*S
    if drawer:
        d.rounded_rectangle([m,20*S,N-m,N-m],radius=6*S,fill=(92,104,150,255),outline=(40,46,80,255),width=S)
        d.rounded_rectangle([m+3*S,24*S,N-m-3*S,N-m-4*S],radius=4*S,fill=(120,134,186,255))
        d.rounded_rectangle([24*S,44*S,40*S,50*S],radius=S,fill=(230,232,240,255),outline=(40,46,80,255),width=S)
        hexagon(d,32*S,20*S,13*S,fill=(55,142,240,255),outline=(20,60,130,255))
        globe(d,32*S,20*S,7*S,S,(255,255,255,255))
    else:
        grad=Image.new('RGBA',(N,N)); gd=ImageDraw.Draw(grad)
        for y in range(N):
            t=y/N; gd.line([0,y,N,y],fill=(int(70+(20-70)*t),int(160+(70-160)*t),int(250+(170-250)*t),255))
        mask=Image.new('L',(N,N),0); ImageDraw.Draw(mask).rounded_rectangle([m,m,N-m,N-m],radius=12*S,fill=255)
        im.paste(grad,(0,0),mask)
        d.rounded_rectangle([m,m,N-m,N-m],radius=12*S,outline=(15,45,110,255),width=S)
        hexagon(d,28*S,27*S,15*S,outline=(255,255,255,235),width=2*S)
        globe(d,28*S,27*S,8*S,int(1.5*S),(255,255,255,255))
        d.text((36*S,36*S),"Br",font=font(17*S),fill=(255,255,255,255),stroke_width=S,stroke_fill=(15,45,110,255))
    if selected:
        a=im.split()[3].filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.GaussianBlur(3*S))
        glow=Image.new('RGBA',(N,N),(255,230,60,0)); glow.putalpha(a.point(lambda v:min(255,v*2)))
        glow.alpha_composite(im); im=glow
    return im.resize((64,64),Image.LANCZOS)
def png(im):
    b=io.BytesIO(); im.save(b,'PNG',optimize=True); return b.getvalue()
open('dist/info/AmiSurf.info','wb').write(png(tile(0))+png(tile(1)))
open('dist/info/drawer.info','wb').write(png(tile(0,True))+png(tile(1,True)))
tile(0).save('ui/icons/about.png',optimize=True)
Image.open('assets/about-banner-512.png').convert('RGBA').save('ui/icons/splash.png',optimize=True)
print('ok')
