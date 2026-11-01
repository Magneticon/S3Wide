#ifndef S3_TEXT_H
#define S3_TEXT_H

#include "s3_raster.h"

/* ExtTextOut wOptions. */
#define S3_ETO_OPAQUE  0x0002U
#define S3_ETO_CLIPPED 0x0004U

/* DRAWMODE bkMode. */
#define S3_BK_TRANSPARENT 1
#define S3_BK_OPAQUE      2

/* GDI raster font as passed to the driver: lpFontInfo points at fsType,
   66 bytes into the font segment (fsVersion is at offset 0). Version 2.00
   fonts keep a 4-byte {width, offset} table at segment offset 118,
   version 3.00 fonts a 6-byte {width, 32-bit offset} table at 148.
   Glyphs are column-major byte columns, bit 7 = leftmost pixel. */
#define S3_FONT_INFO_OFFSET 66U

/* ExtTextOut core. With count < 0 nothing is drawn and the extent of
   -count characters is returned; otherwise the string is drawn with its
   cell's top-left at (x, y). Returns height << 16 | width, or 0 on error. */
S3_U32 S3_TextOut(void S3_FAR *device, S3_I16 x, S3_I16 y,
                  const S3_RECT16 S3_FAR *clip, const S3_U8 S3_FAR *text,
                  S3_I16 count, const void S3_FAR *font_info,
                  S3_DRAWMODE16 S3_FAR *mode,
                  const S3_I16 S3_FAR *char_widths,
                  const S3_RECT16 S3_FAR *opaque_rect, S3_U16 options);

/* GetCharWidth core: widths of characters first..last into buffer. */
int S3_TextCharWidths(const void S3_FAR *font_info, S3_U16 first,
                      S3_U16 last, S3_I16 S3_FAR *buffer);

#endif
