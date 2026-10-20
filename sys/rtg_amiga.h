/* rtg_amiga.h - our own minimal RTG bindings (no third-party headers).
   Declares only what AmiSurf uses, from the public library ABIs:
   Picasso96API.library: p96GetBitMapAttr (LVO -42), p96WritePixelArray (LVO -102)
   cybergraphics.library: GetCyberMapAttr (LVO -96), WritePixelArray (LVO -126)
   Written for this project; public domain / same licence as the browser. */
#ifndef ASB_RTG_AMIGA_H
#define ASB_RTG_AMIGA_H
#include <exec/types.h>
#include <inline/macros.h>
struct BitMap; struct RastPort;
/* Picasso96 */
#define ASB_P96BMA_ISP96   8UL   /* 9th attribute of p96GetBitMapAttr */
#define ASB_RGBFB_R8G8B8   2UL
#define ASB_P96BMA_RGBFORMAT 7UL /* 8th attribute: RGBFTYPE of the bitmap */
struct asb_p96_renderinfo { APTR Memory; WORD BytesPerRow; WORD pad; ULONG RGBFormat; };
extern struct Library *P96Base;
#define asb_p96GetBitMapAttr(bm,attr) \
  LP2(0x2a, ULONG, p96GetBitMapAttr, struct BitMap *, bm, a0, ULONG, attr, d0, , P96Base)
#define asb_p96WritePixelArray(ri,sx,sy,rp,dx,dy,w,h) \
  LP8NR(0x66, p96WritePixelArray, struct asb_p96_renderinfo *, ri, a0, UWORD, sx, d0, UWORD, sy, d1, \
        struct RastPort *, rp, a1, UWORD, dx, d2, UWORD, dy, d3, UWORD, w, d4, UWORD, h, d5, , P96Base)
/* CyberGraphX */
#define ASB_CYBRMATTR_ISCYBERGFX 0x80000008UL
#define ASB_RECTFMT_RGB 0UL
extern struct Library *CyberGfxBase;
#define asb_GetCyberMapAttr(bm,attr) \
  LP2(0x60, ULONG, GetCyberMapAttr, struct BitMap *, bm, a0, ULONG, attr, d0, , CyberGfxBase)
#define asb_cgxWritePixelArray(src,sx,sy,mod,rp,dx,dy,w,h,fmt) \
  LP10(0x7e, ULONG, WritePixelArray, APTR, src, a0, UWORD, sx, d0, UWORD, sy, d1, UWORD, mod, d2, \
       struct RastPort *, rp, a1, UWORD, dx, d3, UWORD, dy, d4, UWORD, w, d5, UWORD, h, d6, UBYTE, fmt, d7, , CyberGfxBase)
#endif
