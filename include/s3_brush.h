#ifndef S3_BRUSH_H
#define S3_BRUSH_H

#include "s3_abi.h"

#define S3_OBJ_BRUSH   2U
#define S3_OBJ_FONT    3U

#define S3_BS_SOLID    0U
#define S3_BS_HOLLOW   1U
#define S3_BS_HATCHED  2U
#define S3_BS_PATTERN  3U

#define S3_BRUSH_SIZE  8

#pragma pack(push, 2)
/* Windows 3.1 logical brush passed to RealizeObject(OBJ_BRUSH). For
   BS_PATTERN, lbColor holds a far pointer to the pattern's PBITMAP. */
typedef struct S3_LBRUSH16 {
    S3_U16 lbStyle;
    S3_U32 lbColor;
    S3_I16 lbHatch;
    S3_U32 lbBkColor;
} S3_LBRUSH16;

/* Driver-owned physical brush: an 8x8 tile of pixel values (see
   s3_pixel.h). Hatched brushes also keep a 1-bit mask (bit 7 = leftmost
   pixel, 1 = foreground) so a transparent background mode can skip
   background pixels. */
typedef struct S3_PBRUSH16 {
    S3_U16 style;
    S3_U16 has_mask;
    S3_U32 color[S3_BRUSH_SIZE * S3_BRUSH_SIZE];
    S3_U8 mask[S3_BRUSH_SIZE];
} S3_PBRUSH16;
#pragma pack(pop)

typedef char S3_ASSERT_LBRUSH_12[(sizeof(S3_LBRUSH16) == 12) ? 1 : -1];

/* Returns nonzero when the logical brush was converted. */
int S3_RealizeBrush(const S3_LBRUSH16 S3_FAR *logical,
                    S3_PBRUSH16 S3_FAR *physical);

/* An 8x8 pattern from the top-left of a monochrome bitmap or one in the
   display's pixel format. Monochrome bits expand to white (1) and black
   (0), as GDI expects. */
int S3_RealizePatternBrush(const S3_BITMAP16 S3_FAR *bitmap,
                           S3_PBRUSH16 S3_FAR *physical);

#endif
