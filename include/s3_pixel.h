#ifndef S3_PIXEL_H
#define S3_PIXEL_H

#include "s3_mode.h"

/* A pixel value is the colour as stored in the frame buffer: an 8-bpp
   colour-cube index, a 5:6:5 word, or 0x00RRGGBB, kept little-endian in
   s3_mode.bytes_pixel bytes. A physical colour (what ColorInfo hands GDI)
   is the pixel value itself in the direct-colour modes; at 8 bpp it also
   carries the 0xFF000000 index marker. */
S3_U32 S3_PixelLoad(const S3_U8 S3_FAR *p, S3_U16 bytes_pixel);
/* Inline forms for hot loops (x86 is little-endian, so a pixel is one
   byte, word or dword). */
#define S3_PIXEL_GET(p, bp) \
    ((bp) == 4 ? *(const S3_U32 S3_FAR *)(p) \
               : (bp) == 2 ? (S3_U32)*(const S3_U16 S3_FAR *)(p) : (S3_U32)*(p))
#define S3_PIXEL_PUT(p, v, bp) \
    do { \
        if ((bp) == 4) *(S3_U32 S3_FAR *)(p) = (v); \
        else if ((bp) == 2) *(S3_U16 S3_FAR *)(p) = (S3_U16)(v); \
        else *(p) = (S3_U8)(v); \
    } while (0)
void S3_PixelStore(S3_U8 S3_FAR *p, S3_U32 pixel, S3_U16 bytes_pixel);
S3_U32 S3_RGBToPixel(S3_U32 rgb);
S3_U32 S3_PixelToRGB(S3_U32 pixel);
S3_U32 S3_PhysicalToPixel(S3_U32 physical);
S3_U32 S3_PixelToPhysical(S3_U32 pixel);
/* Monochrome conversion of a pixel: nonzero when it is at least half
   bright (luma 30/59/11). */
int S3_PixelIsLight(S3_U32 pixel);

#endif
