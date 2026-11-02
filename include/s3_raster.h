#ifndef S3_RASTER_H
#define S3_RASTER_H

#include "s3_surface.h"
#include "s3_brush.h"

/* A drawing target: the banked screen or a memory bitmap, monochrome or in
   the display's pixel format. Memory bitmaps may be huge (bmSegmentIndex
   != 0): rows then sit scan_segment to a segment, never crossing one. */
#define S3_TARGET_SCREEN 1U
#define S3_TARGET_MEMC   2U  /* colour, display pixel format */
#define S3_TARGET_MEM1   3U

typedef struct S3_TARGET {
    S3_U16 kind;
    S3_U16 width;
    S3_U16 height;
    S3_U16 stride;
    S3_U16 bytes_pixel;      /* row format: 1 for monochrome targets */
    S3_U16 segment_index;    /* selector step between segments, 0 = one */
    S3_U16 scan_segment;     /* rows per segment when segment_index != 0 */
    S3_SURFACE S3_FAR *surface;
    S3_U8 S3_FAR *bits;
} S3_TARGET;

/* Distinguishes the display PDEVICE (pdType 1) from a PBITMAP (bmType 0). */
int S3_BindTarget(void S3_FAR *device, S3_TARGET S3_FAR *target);

/* Rows in "target format": colour targets hold pixel values of
   bytes_pixel bytes each, monochrome targets hold one byte per pixel,
   0x00 (black) or 0xFF (white). The caller keeps x..x+count-1 and y inside
   the target. */
int S3_TargetReadRow(const S3_TARGET S3_FAR *target, S3_I16 x, S3_I16 y,
                     S3_U16 count, S3_U8 S3_FAR *out);
int S3_TargetWriteRow(const S3_TARGET S3_FAR *target, S3_I16 x, S3_I16 y,
                      S3_U16 count, const S3_U8 S3_FAR *in);

/* Address of colour pixel (x, y), mapping its bank on the screen; NULL for
   monochrome targets (bit-packed) or when the bank cannot be mapped. */
S3_U8 S3_FAR *S3_TargetPixel(const S3_TARGET S3_FAR *target, S3_I16 x, S3_I16 y);

/* Ternary raster operation, bitwise: bit i of rop selects the minterm with
   P = bit 2, S = bit 1, D = bit 0 of i (PATCOPY F0, SRCCOPY CC, DSTINVERT 55). */
S3_U8 S3_Rop3(S3_U8 rop, S3_U8 pattern, S3_U8 source, S3_U8 destination);
S3_U32 S3_Rop3Long(S3_U8 rop, S3_U32 pattern, S3_U32 source,
                   S3_U32 destination);

/* d = rop(p, s, d) bytewise over row buffers; p and s may be NULL when the
   ROP does not use them. */
void S3_RopBytes(S3_U8 rop, S3_U8 S3_FAR *d, const S3_U8 S3_FAR *p,
                 const S3_U8 S3_FAR *s, S3_U16 bytes);

/* Fill count pixels of row buffer out with pixel (bytes_pixel each). */
void S3_FillPixels(S3_U8 S3_FAR *out, S3_U32 pixel, S3_U16 count,
                   S3_U16 bytes_pixel);

/* General BitBlt: any ROP3 between brush, source and destination, with
   GDI's monochrome/colour conversions (mono 1 = background colour, 0 = text
   colour; colour -> mono is 1 where the pixel equals the background colour).
   Returns nonzero on success. */
int S3_RasterBitBlt(void S3_FAR *destination, S3_I16 dx, S3_I16 dy,
                    void S3_FAR *source, S3_I16 sx, S3_I16 sy,
                    S3_I16 width, S3_I16 height, S3_U32 rop3,
                    const S3_PBRUSH16 S3_FAR *brush,
                    const S3_DRAWMODE16 S3_FAR *mode);

#endif
