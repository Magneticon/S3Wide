#ifndef S3_DIB_H
#define S3_DIB_H

#include "s3_raster.h"

/* Uncompressed (BI_RGB) bottom-up DIBs with 1, 4, 8 or 24 bits per pixel. */
typedef struct S3_DIB_FORMAT {
    S3_U16 width;
    S3_U16 height;
    S3_U16 bits;
    S3_U16 colors;      /* colour-table entries (0 for 24 bpp) */
    S3_U32 stride;      /* bytes per scan, DWORD aligned */
} S3_DIB_FORMAT;

int S3_DibFormat(const S3_BITMAPINFO16 S3_FAR *info, S3_DIB_FORMAT S3_FAR *format);

/* DeviceBitmapBits: copy cScans DIB scans starting at DIB scan iStart
   (scan 0 is the bottom row) into (fGet == 0) or out of (fGet != 0) a
   memory bitmap. On Get the colour table is filled with the device colours.
   Returns the number of scans copied, 0 on failure. */
S3_U16 S3_DibBitmapBits(void S3_FAR *bitmap, S3_U16 get, S3_U16 start,
                        S3_U16 scans, S3_U8 S3_FAR *bits,
                        S3_BITMAPINFO16 S3_FAR *info);

/* SetDIBitsToDevice core: draw DIB scans [start, start + scans) with their
   top-left at (x, y) in device coordinates of the full DIB, clipped. */
S3_U16 S3_DibToDevice(void S3_FAR *device, S3_I16 x, S3_I16 y,
                      S3_U16 start, S3_U16 scans,
                      const S3_RECT16 S3_FAR *clip,
                      const S3_U8 S3_FAR *bits,
                      const S3_BITMAPINFO16 S3_FAR *info);

#endif
