#include "s3_dib.h"
#if !defined(S3_HOST_TEST)
#include <i86.h>
/* KERNEL's selector increment for walking huge (> 64 KiB) memory. */
extern void S3_FAR S3_PASCAL __AHINCR(void);
#endif

#define S3_DIB_ROW_MAX 1280

static S3_U8 s3_dib_row[S3_DIB_ROW_MAX * 3];
static S3_U8 s3_device_row[S3_DIB_ROW_MAX];
static S3_U8 s3_table_index[256];
static S3_U8 s3_table_mono[256];

/* The 16 VGA colours, used to pick indices when GetDIBits asks for 4 bpp. */
static const S3_U8 s3_vga16[16][3] = {
    {0, 0, 0}, {128, 0, 0}, {0, 128, 0}, {128, 128, 0},
    {0, 0, 128}, {128, 0, 128}, {0, 128, 128}, {192, 192, 192},
    {128, 128, 128}, {255, 0, 0}, {0, 255, 0}, {255, 255, 0},
    {0, 0, 255}, {255, 0, 255}, {0, 255, 255}, {255, 255, 255}
};

int S3_DibFormat(const S3_BITMAPINFO16 S3_FAR *info, S3_DIB_FORMAT S3_FAR *format)
{
    const S3_BITMAPINFOHEADER16 S3_FAR *header;
    S3_U32 colors;

    if (info == (const S3_BITMAPINFO16 S3_FAR *)0) return 0;
    header = &info->bmiHeader;
    if (header->biSize < sizeof(S3_BITMAPINFOHEADER16) ||
        header->biWidth == 0 || header->biWidth > S3_DIB_ROW_MAX * 8UL ||
        header->biHeight == 0 || header->biHeight > 32767UL ||
        header->biPlanes != 1 || header->biCompression != 0) {
        return 0;
    }
    switch (header->biBitCount) {
    case 1: case 4: case 8:
        colors = header->biClrUsed != 0 ? header->biClrUsed : (1UL << header->biBitCount);
        if (colors > (1UL << header->biBitCount)) return 0;
        break;
    case 24:
        colors = 0;
        break;
    default:
        return 0;
    }
    format->width = (S3_U16)header->biWidth;
    format->height = (S3_U16)header->biHeight;
    format->bits = header->biBitCount;
    format->colors = (S3_U16)colors;
    format->stride = (((S3_U32)format->width * format->bits + 31UL) / 32UL) * 4UL;
    return 1;
}

static S3_U8 s3_rgb_to_mono(S3_U8 red, S3_U8 green, S3_U8 blue)
{
    S3_U16 luma = (S3_U16)(((S3_U16)red * 30U + (S3_U16)green * 59U +
                            (S3_U16)blue * 11U) / 100U);
    return (S3_U8)(luma >= 128U ? 0xFF : 0x00);
}

static S3_U8 s3_rgb_to_index(S3_U8 red, S3_U8 green, S3_U8 blue)
{
    S3_U8 index = 0;
    (void)S3_RGBToColorIndex((S3_U32)red | ((S3_U32)green << 8) |
                             ((S3_U32)blue << 16), &index);
    return index;
}

/* Address of byte `offset` of a possibly huge DIB, and how many bytes follow
   it in the same segment. */
static S3_U8 S3_FAR *s3_dib_address(const S3_U8 S3_FAR *bits, S3_U32 offset,
                                    S3_U32 S3_FAR *contiguous)
{
#if defined(S3_HOST_TEST)
    *contiguous = 0xFFFFFFFFUL;
    return (S3_U8 S3_FAR *)bits + offset;
#else
    S3_U32 linear = (S3_U32)FP_OFF(bits) + offset;
    S3_U16 increment = (S3_U16)(S3_U32)(void S3_FAR *)__AHINCR;
    S3_U16 segment = (S3_U16)(FP_SEG(bits) + (S3_U16)(linear >> 16) * increment);
    *contiguous = 65536UL - (linear & 0xFFFFUL);
    return (S3_U8 S3_FAR *)MK_FP(segment, (S3_U16)linear);
#endif
}

/* Copy one DIB scan between the huge DIB and a near row buffer. */
static void s3_dib_scan(S3_U8 S3_FAR *bits, S3_U32 offset, S3_U8 S3_FAR *row,
                        S3_U16 bytes, int to_dib)
{
    S3_U32 contiguous;
    S3_U16 run, i;
    S3_U8 S3_FAR *at;

    while (bytes != 0) {
        at = s3_dib_address(bits, offset, &contiguous);
        run = contiguous < bytes ? (S3_U16)contiguous : bytes;
        for (i = 0; i < run; ++i) {
            if (to_dib) at[i] = row[i];
            else row[i] = at[i];
        }
        row += run;
        offset += run;
        bytes = (S3_U16)(bytes - run);
    }
}

static void s3_build_tables(const S3_BITMAPINFO16 S3_FAR *info,
                            const S3_DIB_FORMAT S3_FAR *format)
{
    S3_U16 i;
    const S3_RGBQUAD16 S3_FAR *quad;
    for (i = 0; i < format->colors; ++i) {
        quad = &info->bmiColors[i];
        s3_table_index[i] = s3_rgb_to_index(quad->rgbRed, quad->rgbGreen, quad->rgbBlue);
        s3_table_mono[i] = s3_rgb_to_mono(quad->rgbRed, quad->rgbGreen, quad->rgbBlue);
    }
}

/* Convert one DIB scan (already in s3_dib_row) to target format. */
static void s3_dib_to_target(const S3_DIB_FORMAT S3_FAR *format, S3_U16 kind,
                             S3_U16 count)
{
    S3_U16 i, value;
    const S3_U8 *table = kind == S3_TARGET_MEM1 ? s3_table_mono : s3_table_index;
    for (i = 0; i < count; ++i) {
        switch (format->bits) {
        case 1:
            value = (s3_dib_row[i >> 3] >> (7 - (i & 7))) & 1;
            s3_device_row[i] = table[value];
            break;
        case 4:
            value = (i & 1) ? (s3_dib_row[i >> 1] & 0x0F) : (s3_dib_row[i >> 1] >> 4);
            s3_device_row[i] = table[value];
            break;
        case 8:
            s3_device_row[i] = table[s3_dib_row[i]];
            break;
        default: /* 24 bpp: B, G, R */
            if (kind == S3_TARGET_MEM1) {
                s3_device_row[i] = s3_rgb_to_mono(s3_dib_row[i * 3 + 2],
                                                  s3_dib_row[i * 3 + 1],
                                                  s3_dib_row[i * 3]);
            } else {
                s3_device_row[i] = s3_rgb_to_index(s3_dib_row[i * 3 + 2],
                                                   s3_dib_row[i * 3 + 1],
                                                   s3_dib_row[i * 3]);
            }
            break;
        }
    }
}

static S3_U8 s3_nearest_vga16(S3_U32 rgb)
{
    S3_U16 i, best = 0;
    S3_U32 best_distance = 0xFFFFFFFFUL, distance;
    S3_I16 dr, dg, db;
    for (i = 0; i < 16; ++i) {
        dr = (S3_I16)((S3_I16)(rgb & 0xFF) - s3_vga16[i][0]);
        dg = (S3_I16)((S3_I16)((rgb >> 8) & 0xFF) - s3_vga16[i][1]);
        db = (S3_I16)((S3_I16)((rgb >> 16) & 0xFF) - s3_vga16[i][2]);
        distance = (S3_U32)((S3_I32)dr * dr) + (S3_U32)((S3_I32)dg * dg) +
                   (S3_U32)((S3_I32)db * db);
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    return (S3_U8)best;
}

/* Convert one target row (s3_device_row) into DIB format in s3_dib_row. */
static void s3_target_to_dib(const S3_DIB_FORMAT S3_FAR *format, S3_U16 kind,
                             S3_U16 count)
{
    S3_U16 i;
    S3_U32 rgb;
    S3_U8 value;
    for (i = 0; i < (S3_U16)format->stride && i < sizeof(s3_dib_row); ++i) {
        s3_dib_row[i] = 0;
    }
    for (i = 0; i < count; ++i) {
        if (kind == S3_TARGET_MEM1) {
            rgb = s3_device_row[i] ? 0x00FFFFFFUL : 0;
        } else {
            (void)S3_ColorIndexToRGB(s3_device_row[i], &rgb);
        }
        switch (format->bits) {
        case 1:
            value = (S3_U8)((rgb & 0xFF) + ((rgb >> 8) & 0xFF) + ((rgb >> 16) & 0xFF) >= 384U);
            s3_dib_row[i >> 3] |= (S3_U8)(value << (7 - (i & 7)));
            break;
        case 4:
            value = s3_nearest_vga16(rgb);
            s3_dib_row[i >> 1] |= (S3_U8)((i & 1) ? value : (value << 4));
            break;
        case 8:
            s3_dib_row[i] = kind == S3_TARGET_MEM1 ? (S3_U8)(s3_device_row[i] ? 0xFF : 0)
                                                   : s3_device_row[i];
            break;
        default:
            s3_dib_row[i * 3] = (S3_U8)(rgb >> 16);
            s3_dib_row[i * 3 + 1] = (S3_U8)(rgb >> 8);
            s3_dib_row[i * 3 + 2] = (S3_U8)rgb;
            break;
        }
    }
}

static void s3_fill_color_table(S3_BITMAPINFO16 S3_FAR *info,
                                const S3_DIB_FORMAT S3_FAR *format)
{
    S3_U16 i;
    S3_U32 rgb;
    S3_RGBQUAD16 S3_FAR *quad;
    for (i = 0; i < format->colors; ++i) {
        quad = &info->bmiColors[i];
        if (format->bits == 1) {
            rgb = i ? 0x00FFFFFFUL : 0;
        } else if (format->bits == 4) {
            rgb = (S3_U32)s3_vga16[i][0] | ((S3_U32)s3_vga16[i][1] << 8) |
                  ((S3_U32)s3_vga16[i][2] << 16);
        } else {
            (void)S3_ColorIndexToRGB((S3_U8)i, &rgb);
        }
        quad->rgbRed = (S3_U8)rgb;
        quad->rgbGreen = (S3_U8)(rgb >> 8);
        quad->rgbBlue = (S3_U8)(rgb >> 16);
        quad->rgbReserved = 0;
    }
}

S3_U16 S3_DibBitmapBits(void S3_FAR *bitmap, S3_U16 get, S3_U16 start,
                        S3_U16 scans, S3_U8 S3_FAR *bits,
                        S3_BITMAPINFO16 S3_FAR *info)
{
    S3_TARGET target;
    S3_DIB_FORMAT format;
    S3_U16 scan, count, row_bytes, done = 0;
    S3_I16 y;

    if (bits == (S3_U8 S3_FAR *)0 || !S3_DibFormat(info, &format) ||
        !S3_BindTarget(bitmap, &target) || target.kind == S3_TARGET_SCREEN ||
        format.stride > sizeof(s3_dib_row)) {
        return 0;
    }
    count = format.width < target.width ? format.width : target.width;
    if (count > S3_DIB_ROW_MAX) count = S3_DIB_ROW_MAX;
    row_bytes = (S3_U16)format.stride;
    if (get) {
        s3_fill_color_table(info, &format);
    } else {
        s3_build_tables(info, &format);
    }
    for (scan = 0; scan < scans; ++scan) {
        /* DIB scan (start + scan) counted from the bottom of the bitmap. */
        if ((S3_U32)start + scan >= target.height) break;
        y = (S3_I16)(target.height - 1 - (start + scan));
        if (get) {
            if (!S3_TargetReadRow(&target, 0, y, count, s3_device_row)) break;
            s3_target_to_dib(&format, target.kind, count);
            s3_dib_scan(bits, (S3_U32)scan * format.stride, s3_dib_row, row_bytes, 1);
        } else {
            s3_dib_scan(bits, (S3_U32)scan * format.stride, s3_dib_row, row_bytes, 0);
            s3_dib_to_target(&format, target.kind, count);
            if (!S3_TargetWriteRow(&target, 0, y, count, s3_device_row)) break;
        }
        ++done;
    }
    return done;
}

S3_U16 S3_DibToDevice(void S3_FAR *device, S3_I16 x, S3_I16 y,
                      S3_U16 start, S3_U16 scans,
                      const S3_RECT16 S3_FAR *clip,
                      const S3_U8 S3_FAR *bits,
                      const S3_BITMAPINFO16 S3_FAR *info)
{
    S3_TARGET target;
    S3_DIB_FORMAT format;
    S3_I16 left, right, top, bottom, row_y, first, count;
    S3_U16 scan, done = 0;

    if (bits == (const S3_U8 S3_FAR *)0 || !S3_DibFormat(info, &format) ||
        !S3_BindTarget(device, &target) || format.stride > sizeof(s3_dib_row)) {
        return 0;
    }
    left = x;
    top = 0;
    right = (S3_I16)(x + format.width);
    bottom = (S3_I16)target.height;
    if (left < 0) left = 0;
    if (right > (S3_I16)target.width) right = (S3_I16)target.width;
    if (clip != (const S3_RECT16 S3_FAR *)0) {
        if (clip->left > left) left = clip->left;
        if (clip->right < right) right = clip->right;
        if (clip->top > top) top = clip->top;
        if (clip->bottom < bottom) bottom = clip->bottom;
    }
    if (left >= right) return scans;
    first = (S3_I16)(left - x);
    count = (S3_I16)(right - left);
    if ((S3_I32)first + count > S3_DIB_ROW_MAX) return 0;
    s3_build_tables(info, &format);
    for (scan = 0; scan < scans; ++scan) {
        row_y = (S3_I16)(y + (S3_I16)format.height - 1 - (S3_I16)(start + scan));
        ++done;
        if (row_y < top || row_y >= bottom) continue;
        s3_dib_scan((S3_U8 S3_FAR *)bits, (S3_U32)scan * format.stride, s3_dib_row,
                    (S3_U16)format.stride, 0);
        s3_dib_to_target(&format, target.kind, (S3_U16)(first + count));
        if (!S3_TargetWriteRow(&target, left, row_y, (S3_U16)count,
                               s3_device_row + first)) {
            return 0;
        }
    }
    return done;
}
