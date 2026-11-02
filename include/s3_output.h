#ifndef S3_OUTPUT_H
#define S3_OUTPUT_H

#include "s3_raster.h"

#define S3_OS_SCANLINES 4U
#define S3_OS_BEGINNSCAN 80U
#define S3_OS_ENDNSCAN  81U

/* ROP2 (1..16, R2_BLACK..R2_WHITE) as the equivalent ROP3 code with the pen
   or brush as the pattern operand. Returns 0xFF.. for an invalid code. */
S3_U8 S3_Rop2ToRop3(S3_I16 rop2);

/* OS_SCANLINES: points[0].y is the scan line; points[1..count-1] are
   {x_start, x_end} spans with x_end exclusive. Spans are filled with the
   brush, or with the pen colour when brush is NULL, combined by Rop2.
   Hatched brushes leave their background untouched in TRANSPARENT mode. */
int S3_OutputScanlines(void S3_FAR *destination, S3_U16 count,
                       const S3_POINT16 S3_FAR *points,
                       const S3_PPEN16 S3_FAR *pen,
                       const S3_PBRUSH16 S3_FAR *brush,
                       const S3_DRAWMODE16 S3_FAR *mode,
                       const S3_RECT16 S3_FAR *clip);

/* OS_POLYLINE with a solid one-pixel pen, any Rop2, on any target. */
int S3_OutputPolyline(void S3_FAR *destination, S3_U16 count,
                      const S3_POINT16 S3_FAR *points, S3_U32 physical,
                      S3_I16 rop2, const S3_RECT16 S3_FAR *clip);

#endif
