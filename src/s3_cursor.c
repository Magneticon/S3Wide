#include "s3_cursor.h"

int S3_CursorPattern(const S3_CURSORSHAPE16 S3_FAR *shape,
                     S3_U8 S3_FAR *pattern)
{
    const S3_U8 S3_FAR *and_mask;
    const S3_U8 S3_FAR *xor_mask;
    S3_U16 i, x, y, width, height, index;
    S3_U8 bit;

    if (shape == (const S3_CURSORSHAPE16 S3_FAR *)0 ||
        pattern == (S3_U8 S3_FAR *)0 || shape->csWidth <= 0 ||
        shape->csHeight <= 0 || shape->csWidthBytes <= 0 ||
        (S3_U16)shape->csWidthBytes * 8U < (S3_U16)shape->csWidth) {
        return 0;
    }
    /* Start fully transparent: AND bytes 0xFF, XOR bytes 0x00. */
    for (i = 0; i < S3_HW_CURSOR_BYTES; i += 4) {
        pattern[i] = 0xFF;
        pattern[i + 1] = 0xFF;
        pattern[i + 2] = 0x00;
        pattern[i + 3] = 0x00;
    }
    width = (S3_U16)shape->csWidth < S3_HW_CURSOR_SIZE ? (S3_U16)shape->csWidth
                                                       : S3_HW_CURSOR_SIZE;
    height = (S3_U16)shape->csHeight < S3_HW_CURSOR_SIZE ? (S3_U16)shape->csHeight
                                                         : S3_HW_CURSOR_SIZE;
    and_mask = (const S3_U8 S3_FAR *)(shape + 1);
    xor_mask = and_mask + (S3_U16)shape->csWidthBytes * (S3_U16)shape->csHeight;
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            bit = (S3_U8)(0x80U >> (x & 7));
            /* Row y, group x/16, byte (x % 16) / 8 of the AND pair. */
            index = (S3_U16)(y * 16U + (x >> 4) * 4U + ((x >> 3) & 1));
            if (!(and_mask[y * (S3_U16)shape->csWidthBytes + (x >> 3)] & bit)) {
                pattern[index] = (S3_U8)(pattern[index] & (S3_U8)~bit);
            }
            if (xor_mask[y * (S3_U16)shape->csWidthBytes + (x >> 3)] & bit) {
                pattern[index + 2] = (S3_U8)(pattern[index + 2] | bit);
            }
        }
    }
    return 1;
}
