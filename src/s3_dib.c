#include "s3_dib.h"
#include "s3_pixel.h"
#include "s3_copy.h"
#if !defined(S3_HOST_TEST)
#include <i86.h>
/* KERNEL's selector increment for walking huge (> 64 KiB) memory. */
extern void S3_FAR S3_PASCAL __AHINCR(void);
#endif
#define s3_fmemcpy S3_COPY
#define s3_fmemset S3_FILL

#define S3_DIB_ROW_MAX S3_ROW_PIXELS_MAX

static S3_U8 S3_FAR_DATA s3_dib_row[S3_ROW_BYTES_MAX];
static S3_U8 S3_FAR_DATA s3_device_row[S3_ROW_BYTES_MAX];
static S3_U8 S3_FAR_DATA s3_index_row[S3_DIB_ROW_MAX];
static S3_U32 s3_table_pixel[256];
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
        header->biPlanes != 1) {
        return 0;
    }
    switch (header->biCompression) {
    case S3_BI_RGB:
        break;
    case S3_BI_RLE8:
    case S3_BI_RLE4:
        /* Run-length streams decode to one index per byte in a row buffer,
           and the stream's length must be known to bound the reads. */
        if (header->biBitCount != (header->biCompression == S3_BI_RLE8 ? 8 : 4) ||
            header->biWidth > S3_DIB_ROW_MAX || header->biSizeImage == 0) {
            return 0;
        }
        break;
    default:
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
    format->compression = (S3_U16)header->biCompression;
    format->image_bytes = header->biSizeImage;
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
    S3_U16 run;
    S3_U8 S3_FAR *at;

    while (bytes != 0) {
        at = s3_dib_address(bits, offset, &contiguous);
        run = contiguous < bytes ? (S3_U16)contiguous : bytes;
        if (to_dib) s3_fmemcpy(at, row, run);
        else s3_fmemcpy(row, at, run);
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
        s3_table_pixel[i] = S3_RGBToPixel((S3_U32)quad->rgbRed |
                                          ((S3_U32)quad->rgbGreen << 8) |
                                          ((S3_U32)quad->rgbBlue << 16));
        s3_table_mono[i] = s3_rgb_to_mono(quad->rgbRed, quad->rgbGreen, quad->rgbBlue);
    }
    /* Indices past a short colour table (biClrUsed) draw black, not the
       colours of an earlier call. */
    for (; i < 256; ++i) {
        s3_table_pixel[i] = S3_RGBToPixel(0);
        s3_table_mono[i] = 0;
    }
}

/* Run-length (BI_RLE8 / BI_RLE4) decoding. A compressed stream cannot be
   addressed by scan, so every call decodes from the start of the image,
   one scan at a time, bottom scan first. */
typedef struct S3_RLE_READER {
    const S3_U8 S3_FAR *bits;   /* start of the image */
    S3_U32 size;                /* biSizeImage */
    S3_U32 pos;                 /* next byte */
    const S3_U8 S3_FAR *at;     /* next byte, within one segment */
    S3_U16 avail;               /* bytes left at `at` before a refill */
    S3_U16 x, y;                /* next pixel: column, scan from the bottom */
    S3_U16 ended;               /* end of bitmap or of the data */
} S3_RLE_READER;

/* 1 where the current decoded scan wrote a pixel. */
static S3_U8 S3_FAR_DATA s3_rle_mask[S3_DIB_ROW_MAX];

static void s3_rle_open(S3_RLE_READER *reader, const S3_U8 S3_FAR *bits, S3_U32 size)
{
    reader->bits = bits;
    reader->size = size;
    reader->pos = 0;
    reader->at = bits;
    reader->avail = 0;
    reader->x = 0;
    reader->y = 0;
    reader->ended = 0;
}

static int s3_rle_byte(S3_RLE_READER *reader, S3_U8 *value)
{
    S3_U32 contiguous;
    if (reader->pos >= reader->size) return 0;
    if (reader->avail == 0) {
        reader->at = s3_dib_address(reader->bits, reader->pos, &contiguous);
        if (contiguous > reader->size - reader->pos) contiguous = reader->size - reader->pos;
        reader->avail = contiguous > 0xFFFFUL ? 0xFFFFU : (S3_U16)contiguous;
    }
    *value = *reader->at++;
    --reader->avail;
    ++reader->pos;
    return 1;
}

static void s3_rle_put(S3_RLE_READER *reader, S3_U16 width, S3_U8 index)
{
    if (reader->x < width) {
        s3_dib_row[reader->x] = index;
        s3_rle_mask[reader->x] = 1;
    }
    if (reader->x != 0xFFFFU) ++reader->x;
}

/* Decode DIB scan `scan` into s3_dib_row (one index per byte) and
   s3_rle_mask. Scans must be requested in order from 0. Pixels the stream
   skips (delta records, a scan or bitmap ended early, truncated data) keep
   mask 0 and callers leave them unchanged, as Windows does. Pixels past
   the width are dropped. */
static void s3_rle_scan(S3_RLE_READER *reader, S3_U16 scan, S3_U16 width, int rle4)
{
    S3_U8 count, code, value = 0, dx, dy;
    S3_U16 i;

    s3_fmemset(s3_rle_mask, 0, width);
    if (reader->ended || reader->y != scan) return;
    for (;;) {
        if (!s3_rle_byte(reader, &count) || !s3_rle_byte(reader, &code)) {
            reader->ended = 1;
            return;
        }
        if (count != 0) {
            /* Encoded run: count pixels of one index, or of two
               alternating nibbles for RLE4. */
            for (i = 0; i < count; ++i) {
                s3_rle_put(reader, width,
                           rle4 ? (S3_U8)((i & 1) ? (code & 0x0F) : (code >> 4)) : code);
            }
            continue;
        }
        switch (code) {
        case 0: /* end of scan */
            reader->x = 0;
            ++reader->y;
            return;
        case 1: /* end of bitmap */
            reader->ended = 1;
            return;
        case 2: /* delta: move right dx and up dy scans */
            if (!s3_rle_byte(reader, &dx) || !s3_rle_byte(reader, &dy)) {
                reader->ended = 1;
                return;
            }
            reader->x = (S3_U16)((S3_U32)reader->x + dx > 0xFFFFUL ? 0xFFFFU
                                                                    : reader->x + dx);
            reader->y = (S3_U16)(reader->y + dy);
            if (dy != 0) return;
            break;
        default: /* absolute run of `code` pixels, padded to a word */
            for (i = 0; i < code; ++i) {
                if (!rle4 || (i & 1) == 0) {
                    if (!s3_rle_byte(reader, &value)) {
                        reader->ended = 1;
                        return;
                    }
                }
                s3_rle_put(reader, width,
                           rle4 ? (S3_U8)((i & 1) ? (value & 0x0F) : (value >> 4)) : value);
            }
            if (((rle4 ? (code + 1U) / 2U : (S3_U16)code) & 1U) != 0) {
                (void)s3_rle_byte(reader, &value);
            }
            break;
        }
    }
}

/* Write the masked pixels of s3_device_row[first .. first + count) to
   target row y, starting at target column x. */
static int s3_rle_write(const S3_TARGET S3_FAR *target, S3_I16 x, S3_I16 y,
                        S3_U16 first, S3_U16 count)
{
    S3_U16 i = first, run, end = (S3_U16)(first + count), bp = target->bytes_pixel;
    while (i < end) {
        if (!s3_rle_mask[i]) {
            ++i;
            continue;
        }
        run = i;
        while (run < end && s3_rle_mask[run]) ++run;
        if (!S3_TargetWriteRow(target, (S3_I16)(x + (i - first)), y, (S3_U16)(run - i),
                               s3_device_row + i * bp)) {
            return 0;
        }
        i = run;
    }
    return 1;
}

/* Convert one DIB scan (already in s3_dib_row) to target format. */
/* Convert count pixels of the DIB bytes in s3_dib_row to target format
   in s3_device_row. phase: pixels to skip in the first byte (1 and 4 bpp
   pieces that start inside a byte). */
static void s3_dib_to_target(const S3_DIB_FORMAT S3_FAR *format, S3_U16 kind,
                             S3_U16 count, S3_U16 bp, S3_U16 phase)
{
    S3_U16 i;
    int mono = kind == S3_TARGET_MEM1;
    S3_U8 S3_FAR *out = s3_device_row;
    const S3_U8 S3_FAR *in = s3_dib_row;
    const S3_U8 S3_FAR *index = s3_dib_row;

    if (format->bits == 24) { /* B, G, R */
        if (mono) {
            for (i = 0; i < count; ++i, in += 3) {
                *out++ = s3_rgb_to_mono(in[2], in[1], in[0]);
            }
        } else if (bp == 4) {
            /* 0x00RRGGBB is B, G, R, 0 in memory. */
            for (i = 0; i < count; ++i, in += 3, out += 4) {
                out[0] = in[0];
                out[1] = in[1];
                out[2] = in[2];
                out[3] = 0;
            }
        } else if (bp == 2) {
            for (i = 0; i < count; ++i, in += 3, out += 2) {
                *(S3_U16 S3_FAR *)out = (S3_U16)(((S3_U16)(in[2] >> 3) << 11) |
                                                 ((S3_U16)(in[1] >> 2) << 5) |
                                                 (in[0] >> 3));
            }
        } else {
            for (i = 0; i < count; ++i, in += 3) {
                *out++ = (S3_U8)S3_RGBToPixel((S3_U32)in[2] | ((S3_U32)in[1] << 8) |
                                              ((S3_U32)in[0] << 16));
            }
        }
        return;
    }
    if (format->bits != 8) {
        /* Unpack 1- and 4-bpp indices to one byte each. */
        for (i = 0; i < count; ++i) {
            S3_U16 j = (S3_U16)(i + phase);
            s3_index_row[i] = format->bits == 4
                ? (S3_U8)((j & 1) ? (s3_dib_row[j >> 1] & 0x0F) : (s3_dib_row[j >> 1] >> 4))
                : (S3_U8)((s3_dib_row[j >> 3] >> (7 - (j & 7))) & 1);
        }
        index = s3_index_row;
    }
    if (mono) {
        for (i = 0; i < count; ++i) out[i] = s3_table_mono[index[i]];
    } else if (bp == 4) {
        for (i = 0; i < count; ++i) ((S3_U32 S3_FAR *)out)[i] = s3_table_pixel[index[i]];
    } else if (bp == 2) {
        for (i = 0; i < count; ++i) {
            ((S3_U16 S3_FAR *)out)[i] = (S3_U16)s3_table_pixel[index[i]];
        }
    } else {
        for (i = 0; i < count; ++i) out[i] = (S3_U8)s3_table_pixel[index[i]];
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
/* Convert count target pixels (s3_device_row) into DIB format at the
   start of s3_dib_row; out_bytes bytes are cleared first (pixels plus any
   scan padding the caller will write). */
static void s3_target_to_dib(const S3_DIB_FORMAT S3_FAR *format, S3_U16 kind,
                             S3_U16 count, S3_U16 bp, S3_U16 out_bytes)
{
    S3_U16 i;
    S3_U32 rgb;
    S3_U8 value;
    const S3_U8 S3_FAR *in = s3_device_row;
    S3_U8 S3_FAR *out = s3_dib_row;
    s3_fmemset(s3_dib_row, 0, out_bytes);
    if (format->bits == 24 && kind != S3_TARGET_MEM1 && bp == 4) {
        for (i = 0; i < count; ++i, in += 4, out += 3) {
            out[0] = in[0];
            out[1] = in[1];
            out[2] = in[2];
        }
        return;
    }
    for (i = 0; i < count; ++i) {
        if (kind == S3_TARGET_MEM1) {
            rgb = s3_device_row[i] ? 0x00FFFFFFUL : 0;
        } else {
            rgb = S3_PixelToRGB(S3_PixelLoad(s3_device_row + i * bp, bp));
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
            /* The colour table is the 8-bpp colour cube at every depth. */
            s3_dib_row[i] = kind == S3_TARGET_MEM1 ? (S3_U8)(s3_device_row[i] ? 0xFF : 0)
                                                   : s3_rgb_to_index((S3_U8)rgb,
                                                                     (S3_U8)(rgb >> 8),
                                                                     (S3_U8)(rgb >> 16));
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
    S3_U16 scan, count, piece, n, done = 0;
    S3_U32 byte0, bytes;
    S3_I16 y;

    if (bits == (S3_U8 S3_FAR *)0 || !S3_DibFormat(info, &format) ||
        !S3_BindTarget(bitmap, &target) || target.kind == S3_TARGET_SCREEN) {
        return 0;
    }
    count = format.width < target.width ? format.width : target.width;
    if (format.compression != S3_BI_RGB) {
        /* Compressed input only: GetDIBits into RLE is not offered. */
        S3_RLE_READER reader;
        S3_DIB_FORMAT indices = format;
        S3_U16 end = (S3_U16)(start + scans);
        if (get) return 0;
        if (count > S3_DIB_ROW_MAX) count = S3_DIB_ROW_MAX;
        if (end > target.height || end < start) end = target.height;
        indices.bits = 8;
        s3_build_tables(info, &format);
        s3_rle_open(&reader, bits, format.image_bytes);
        for (scan = 0; scan < end; ++scan) {
            s3_rle_scan(&reader, scan, format.width, format.compression == S3_BI_RLE4);
            if (scan < start) continue;
            y = (S3_I16)(target.height - 1 - scan);
            s3_dib_to_target(&indices, target.kind, count, target.bytes_pixel, 0);
            if (!s3_rle_write(&target, 0, y, 0, count)) break;
            ++done;
        }
        return done;
    }
    if (get) {
        s3_fill_color_table(info, &format);
    } else {
        s3_build_tables(info, &format);
    }
    for (scan = 0; scan < scans; ++scan) {
        /* DIB scan (start + scan) counted from the bottom of the bitmap. */
        if ((S3_U32)start + scan >= target.height) break;
        y = (S3_I16)(target.height - 1 - (start + scan));
        /* Pieces of up to S3_DIB_ROW_MAX pixels; each starts on a byte
           (4096 pixels are whole bytes at every DIB depth). */
        for (piece = 0; piece < count; piece = (S3_U16)(piece + n)) {
            n = (S3_U16)(count - piece < S3_DIB_ROW_MAX ? count - piece : S3_DIB_ROW_MAX);
            byte0 = (S3_U32)piece * format.bits / 8U;
            bytes = ((S3_U32)(piece + n) * format.bits + 7U) / 8U - byte0;
            if (get) {
                /* The last piece also clears the scan's padding. */
                if (piece + n == count) bytes = format.stride - byte0;
                if (!S3_TargetReadRow(&target, (S3_I16)piece, y, n, s3_device_row)) {
                    return done;
                }
                s3_target_to_dib(&format, target.kind, n, target.bytes_pixel, (S3_U16)bytes);
                s3_dib_scan(bits, (S3_U32)scan * format.stride + byte0, s3_dib_row,
                            (S3_U16)bytes, 1);
            } else {
                s3_dib_scan(bits, (S3_U32)scan * format.stride + byte0, s3_dib_row,
                            (S3_U16)bytes, 0);
                s3_dib_to_target(&format, target.kind, n, target.bytes_pixel, 0);
                if (!S3_TargetWriteRow(&target, (S3_I16)piece, y, n, s3_device_row)) {
                    return done;
                }
            }
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
    S3_U16 scan, done = 0, piece, n;
    S3_U32 byte0, bytes, at;

    if (bits == (const S3_U8 S3_FAR *)0 || !S3_DibFormat(info, &format) ||
        !S3_BindTarget(device, &target)) {
        return 0;
    }
    left = x;
    top = 0;
    right = (S3_I32)x + format.width > 32767L ? 32767 : (S3_I16)(x + format.width);
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
    s3_build_tables(info, &format);
    if (format.compression != S3_BI_RGB) {
        S3_RLE_READER reader;
        S3_DIB_FORMAT indices = format;
        S3_U16 end = (S3_U16)(start + scans);
        /* RLE DIBs are at most S3_DIB_ROW_MAX wide (S3_DibFormat). */
        if ((S3_I32)first + count > S3_DIB_ROW_MAX) return 0;
        indices.bits = 8;
        if (end > format.height || end < start) end = format.height;
        s3_rle_open(&reader, bits, format.image_bytes);
        for (scan = 0; scan < end; ++scan) {
            s3_rle_scan(&reader, scan, format.width, format.compression == S3_BI_RLE4);
            if (scan < start) continue;
            ++done;
            row_y = (S3_I16)(y + (S3_I16)format.height - 1 - (S3_I16)scan);
            if (row_y < top || row_y >= bottom) continue;
            s3_dib_to_target(&indices, target.kind, (S3_U16)(first + count),
                             target.bytes_pixel, 0);
            if (!s3_rle_write(&target, left, row_y, (S3_U16)first, (S3_U16)count)) return 0;
        }
        return done;
    }
    for (scan = 0; scan < scans; ++scan) {
        row_y = (S3_I16)(y + (S3_I16)format.height - 1 - (S3_I16)(start + scan));
        ++done;
        if (row_y < top || row_y >= bottom) continue;
        /* Only the visible columns, in pieces of up to S3_DIB_ROW_MAX
           pixels; a 1- or 4-bpp piece may start inside a byte. */
        for (piece = 0; piece < (S3_U16)count; piece = (S3_U16)(piece + n)) {
            n = (S3_U16)((S3_U16)count - piece < S3_DIB_ROW_MAX ? (S3_U16)count - piece
                                                                : S3_DIB_ROW_MAX);
            at = (S3_U32)(S3_U16)first + piece;
            byte0 = at * format.bits / 8U;
            bytes = ((at + n) * format.bits + 7U) / 8U - byte0;
            s3_dib_scan((S3_U8 S3_FAR *)bits, (S3_U32)scan * format.stride + byte0,
                        s3_dib_row, (S3_U16)bytes, 0);
            s3_dib_to_target(&format, target.kind, n, target.bytes_pixel,
                             (S3_U16)((at * format.bits - byte0 * 8U) / format.bits));
            if (!S3_TargetWriteRow(&target, (S3_I16)(left + piece), row_y, n,
                                   s3_device_row)) {
                return 0;
            }
        }
    }
    return done;
}
