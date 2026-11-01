#include "s3_brush.h"

/* Hatch tiles by HS_* index, bit 7 = leftmost pixel of each row. */
static const S3_U8 s3_hatches[6][S3_BRUSH_SIZE] = {
    { 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 }, /* HS_HORIZONTAL */
    { 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08 }, /* HS_VERTICAL */
    { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 }, /* HS_FDIAGONAL */
    { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 }, /* HS_BDIAGONAL */
    { 0x08, 0x08, 0x08, 0x08, 0xFF, 0x08, 0x08, 0x08 }, /* HS_CROSS */
    { 0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81 }  /* HS_DIAGCROSS */
};

static S3_U8 s3_brush_index(S3_U32 color)
{
    S3_U32 physical = 0;
    (void)S3_ColorInfoCore(color, &physical);
    return (S3_U8)physical;
}

static void s3_fill_mask(S3_PBRUSH16 S3_FAR *physical, S3_U8 foreground,
                         S3_U8 background)
{
    S3_U16 row, column;
    for (row = 0; row < S3_BRUSH_SIZE; ++row) {
        for (column = 0; column < S3_BRUSH_SIZE; ++column) {
            physical->color[row * S3_BRUSH_SIZE + column] =
                (physical->mask[row] & (0x80U >> column)) ? foreground : background;
        }
    }
}

int S3_RealizePatternBrush(const S3_BITMAP16 S3_FAR *bitmap,
                           S3_PBRUSH16 S3_FAR *physical)
{
    S3_U16 row, column;
    const S3_U8 S3_FAR *line;

    if (bitmap == (const S3_BITMAP16 S3_FAR *)0 ||
        bitmap->bmBits == (S3_U8 S3_FAR *)0 ||
        bitmap->bmWidth < S3_BRUSH_SIZE || bitmap->bmHeight < S3_BRUSH_SIZE ||
        bitmap->bmPlanes != 1) {
        return 0;
    }
    if (bitmap->bmBitsPixel == 1) {
        if (bitmap->bmWidthBytes < 1) return 0;
        for (row = 0; row < S3_BRUSH_SIZE; ++row) {
            physical->mask[row] = bitmap->bmBits[row * (S3_U16)bitmap->bmWidthBytes];
        }
        s3_fill_mask(physical, 0xFF, 0x00);
        return 1;
    }
    if (bitmap->bmBitsPixel == 8) {
        if (bitmap->bmWidthBytes < S3_BRUSH_SIZE) return 0;
        for (row = 0; row < S3_BRUSH_SIZE; ++row) {
            line = bitmap->bmBits + row * (S3_U16)bitmap->bmWidthBytes;
            for (column = 0; column < S3_BRUSH_SIZE; ++column) {
                physical->color[row * S3_BRUSH_SIZE + column] = line[column];
            }
        }
        return 1;
    }
    return 0;
}

int S3_RealizeBrush(const S3_LBRUSH16 S3_FAR *logical,
                    S3_PBRUSH16 S3_FAR *physical)
{
    S3_U16 i;
    S3_U8 foreground;

    if (logical == (const S3_LBRUSH16 S3_FAR *)0 ||
        physical == (S3_PBRUSH16 S3_FAR *)0) {
        return 0;
    }
    physical->style = logical->lbStyle;
    physical->has_mask = 0;
    for (i = 0; i < S3_BRUSH_SIZE; ++i) {
        physical->mask[i] = 0xFF;
    }
    switch (logical->lbStyle) {
    case S3_BS_SOLID:
        foreground = s3_brush_index(logical->lbColor);
        for (i = 0; i < S3_BRUSH_SIZE * S3_BRUSH_SIZE; ++i) {
            physical->color[i] = foreground;
        }
        return 1;
    case S3_BS_HOLLOW:
        /* Draws nothing; the colour tile is unused. */
        for (i = 0; i < S3_BRUSH_SIZE * S3_BRUSH_SIZE; ++i) {
            physical->color[i] = 0;
        }
        return 1;
    case S3_BS_HATCHED:
        if (logical->lbHatch < 0 || logical->lbHatch > 5) return 0;
        for (i = 0; i < S3_BRUSH_SIZE; ++i) {
            physical->mask[i] = s3_hatches[logical->lbHatch][i];
        }
        physical->has_mask = 1;
        s3_fill_mask(physical, s3_brush_index(logical->lbColor),
                     s3_brush_index(logical->lbBkColor));
        return 1;
    case S3_BS_PATTERN:
#if defined(S3_HOST_TEST)
        /* lbColor is a 16:16 far pointer only on the Win16 target; host tests
           call S3_RealizePatternBrush directly. */
        return 0;
#else
        return S3_RealizePatternBrush(
            (const S3_BITMAP16 S3_FAR *)logical->lbColor, physical);
#endif
    default:
        return 0;
    }
}
