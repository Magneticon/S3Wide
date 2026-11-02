#ifndef S3_SURFACE_H
#define S3_SURFACE_H

#include "s3_abi.h"

typedef S3_U8 S3_FAR * (S3_FAR S3_PASCAL *S3_MAP_BANK)(void S3_FAR *context,
                                                       S3_U32 bank_number);

typedef struct S3_SURFACE {
    S3_U16 width;
    S3_U16 height;
    S3_U16 pitch;
    S3_U16 bytes_pixel;     /* 1 unless the owner sets more after Init */
    S3_U32 framebuffer_bytes;
    S3_U32 bank_bytes;
    S3_U32 aperture_bytes;
    S3_U32 current_bank;
    S3_U8 S3_FAR *window;
    void S3_FAR *context;
    S3_MAP_BANK map_bank;
} S3_SURFACE;

/* A bounded, unpacked 8-bpp DIB view. Row zero is selected by top_down. */
typedef struct S3_DIB8_VIEW {
    S3_U16 width;
    S3_U16 height;
    S3_U16 stride;
    S3_U32 data_bytes;
    S3_U8 S3_FAR *bits;
    S3_U16 top_down;
} S3_DIB8_VIEW;

int S3_SurfaceInit(S3_SURFACE S3_FAR *surface,
                   S3_U16 width, S3_U16 height, S3_U16 pitch,
                   S3_U32 framebuffer_bytes,
                   S3_U32 bank_bytes, S3_U32 aperture_bytes,
                   S3_U8 S3_FAR *initial_window,
                   void S3_FAR *context, S3_MAP_BANK map_bank);
int S3_SurfacePutPixel(S3_SURFACE S3_FAR *surface,
                       S3_I16 x, S3_I16 y, S3_U8 color);
int S3_SurfaceGetPixel(S3_SURFACE S3_FAR *surface,
                       S3_I16 x, S3_I16 y, S3_U8 S3_FAR *color);
int S3_FarSpanFitsSingleSegment(S3_U16 pointer_offset, S3_U32 bytes);
/* Map the bank holding pixel (x, y) (byte x * bytes_pixel of row y) and
   return a pointer to it; *contiguous
   receives the bytes available before the bank ends. NULL if out of range. */
S3_U8 S3_FAR *S3_SurfaceSpan(S3_SURFACE S3_FAR *surface, S3_I16 x, S3_I16 y,
                             S3_U16 S3_FAR *contiguous);
/* Supports only equal-format 8-bpp SRCCOPY from an unpacked DIB view. */
int S3_SurfaceBlitDIB8(S3_SURFACE S3_FAR *destination,
                       S3_I16 destination_x, S3_I16 destination_y,
                       S3_DIB8_VIEW S3_FAR *source,
                       S3_I16 source_x, S3_I16 source_y,
                       S3_U16 width, S3_U16 height, S3_U32 rop3);
/* Copies one bottom-up, 8-bpp BI_RGB DIB band. The supplied bits pointer is
   the first byte of scan_start; the band itself must fit one far segment.
   translate is a 256-entry logical-index to physical-index table. */
S3_U16 S3_SurfaceSetDIB8Band(S3_SURFACE S3_FAR *destination,
                             S3_I32 destination_x, S3_I32 destination_y,
                             S3_U16 bitmap_width, S3_U16 bitmap_height,
                             S3_U16 stride, S3_U16 scan_start,
                             S3_U16 scan_count,
                             S3_U8 S3_FAR *bits, S3_U32 data_bytes,
                             S3_U16 segment_offset,
                             S3_I16 S3_FAR *translate,
                             S3_U16 translate_entries,
                             S3_RECT16 S3_FAR *clip_rect);
int S3_SurfaceDrawLine(S3_SURFACE S3_FAR *surface,
                       S3_I16 x0, S3_I16 y0,
                       S3_I16 x1, S3_I16 y1,
                       S3_U8 color);
/* Draw one DDA polyline segment with Bresenham rounding, a half-open endpoint,
   and clipping to clip_rect intersected with the surface. Failed bank maps may
   leave a prefix of pixels written; callers must not assume atomic drawing. */
/* Visit the pixels of a DDA line segment (first point included, last point
   excluded) inside [left, right) x [top, bottom). plot returns nonzero to
   continue. */
typedef int (*S3_LINE_PLOT)(void S3_FAR *context, S3_I16 x, S3_I16 y);
int S3_LineVisit(S3_I16 x0, S3_I16 y0, S3_I16 x1, S3_I16 y1,
                 S3_I32 left, S3_I32 top, S3_I32 right, S3_I32 bottom,
                 S3_LINE_PLOT plot, void S3_FAR *context);
int S3_SurfaceDrawLineClipped(S3_SURFACE S3_FAR *surface,
                              S3_I16 x0, S3_I16 y0,
                              S3_I16 x1, S3_I16 y1,
                              const S3_RECT16 S3_FAR *clip_rect,
                              S3_U8 color);
int S3_SurfaceFillRect(S3_SURFACE S3_FAR *surface,
                       S3_I16 left, S3_I16 top,
                       S3_I16 right, S3_I16 bottom,
                       S3_U8 color);
int S3_SurfaceFillRectClipped(S3_SURFACE S3_FAR *surface,
                              S3_I16 left, S3_I16 top,
                              S3_I16 right, S3_I16 bottom,
                              S3_U8 color);
/* Copy pixels with clipping against both surfaces. Same-surface copies use
   memmove-style direction to preserve overlapping source pixels. */
int S3_SurfaceCopyRect(S3_SURFACE S3_FAR *destination,
                       S3_I16 destination_x, S3_I16 destination_y,
                       S3_SURFACE S3_FAR *source,
                       S3_I16 source_x, S3_I16 source_y,
                       S3_U16 width, S3_U16 height);

#endif
