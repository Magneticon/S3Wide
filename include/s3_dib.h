#ifndef S3_DIB_H
#define S3_DIB_H

#include "s3_raster.h"

/* Bottom-up DIBs: uncompressed (BI_RGB) with 1, 4, 8 or 24 bits per pixel,
   or run-length encoded (BI_RLE8 at 8 bpp, BI_RLE4 at 4 bpp, up to
   S3_ROW_PIXELS_MAX wide, biSizeImage required). RLE is accepted when
   drawing or setting bits; GetDIBits into RLE is not offered. */
#define S3_BI_RGB  0U
#define S3_BI_RLE8 1U
#define S3_BI_RLE4 2U

typedef struct S3_DIB_FORMAT {
    S3_U16 width;
    S3_U16 height;
    S3_U16 bits;
    S3_U16 colors;      /* colour-table entries (0 for 24 bpp) */
    S3_U32 stride;      /* bytes per uncompressed scan, DWORD aligned */
    S3_U16 compression; /* S3_BI_RGB, S3_BI_RLE8 or S3_BI_RLE4 */
    S3_U32 image_bytes; /* biSizeImage: the RLE stream's length */
} S3_DIB_FORMAT;

int S3_DibFormat(const S3_BITMAPINFO16 S3_FAR *info, S3_DIB_FORMAT S3_FAR *format);

/* DeviceBitmapBits: copy cScans DIB scans starting at DIB scan iStart
   (scan 0 is the bottom row of the DIB, whose top row is bitmap row 0;
   rows past the bitmap are skipped) into (fGet == 0) or out of (fGet != 0)
   a memory bitmap. On Get the colour table is filled with the device colours.
   Returns the number of scans copied, 0 on failure. */
S3_U16 S3_DibBitmapBits(void S3_FAR *bitmap, S3_U16 get, S3_U16 start,
                        S3_U16 scans, S3_U8 S3_FAR *bits,
                        S3_BITMAPINFO16 S3_FAR *info);

/* StretchDIBits core (DIB to device): SRCCOPY of the source rectangle
   (sx, sy from the DIB's bottom-left, as Windows counts bottom-up DIB
   coordinates) into the destination rectangle, enlarging or keeping each
   extent, uncompressed DIBs with a source span up to 4096 pixels. Returns
   the source scans drawn (>0), 0 on error, -1 when GDI should emulate. */
S3_I16 S3_DibStretchToDevice(void S3_FAR *device, S3_I16 dx, S3_I16 dy,
                             S3_I16 dw, S3_I16 dh, S3_I16 sx, S3_I16 sy,
                             S3_I16 sw, S3_I16 sh, const S3_U8 S3_FAR *bits,
                             const S3_BITMAPINFO16 S3_FAR *info, S3_U32 rop3,
                             const S3_RECT16 S3_FAR *clip);

/* SetDIBitsToDevice core: draw DIB scans [start, start + scans) with their
   top-left at (x, y) in device coordinates of the full DIB, clipped. */
S3_U16 S3_DibToDevice(void S3_FAR *device, S3_I16 x, S3_I16 y,
                      S3_U16 start, S3_U16 scans,
                      const S3_RECT16 S3_FAR *clip,
                      const S3_U8 S3_FAR *bits,
                      const S3_BITMAPINFO16 S3_FAR *info);

#endif
