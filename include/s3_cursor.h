#ifndef S3_CURSOR_H
#define S3_CURSOR_H

#include "s3_abi.h"

/* Windows 3.1 CURSORSHAPE header passed to SetCursor; the AND mask
   (csHeight rows of csWidthBytes) follows it, then the XOR mask. */
#pragma pack(push, 2)
typedef struct S3_CURSORSHAPE16 {
    S3_I16 csHotX;
    S3_I16 csHotY;
    S3_I16 csWidth;
    S3_I16 csHeight;
    S3_I16 csWidthBytes;
    S3_I16 csColor;
} S3_CURSORSHAPE16;
#pragma pack(pop)

/* S3 Trio hardware cursor: 64x64 pixels, 1024 bytes. Each row is four
   16-pixel groups of two AND bytes followed by two XOR bytes (bit 7 =
   leftmost). AND 1/XOR 0 shows the screen, AND 1/XOR 1 inverts it, AND 0
   selects the background (XOR 0) or foreground (XOR 1) colour. */
#define S3_HW_CURSOR_SIZE  64
#define S3_HW_CURSOR_BYTES 1024U

/* Convert a monochrome CURSORSHAPE into the hardware pattern. Pixels
   outside the shape are transparent. Returns nonzero on success. */
int S3_CursorPattern(const S3_CURSORSHAPE16 S3_FAR *shape,
                     S3_U8 S3_FAR *pattern);

#endif
