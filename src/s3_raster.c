#include "s3_raster.h"
#include "s3_trace.h"
#include <string.h>
#if !defined(S3_HOST_TEST)
#include <i86.h>
#define s3_fmemcpy _fmemcpy
#define s3_fmemset _fmemset
#else
#define s3_fmemcpy memcpy
#define s3_fmemset memset
#endif

/* Widest run processed per pass; wider blits are split into chunks. */
#define S3_ROW_MAX 1280

static S3_U8 s3_dst_row[S3_ROW_MAX];
static S3_U8 s3_src_row[S3_ROW_MAX];

int S3_BindTarget(void S3_FAR *device, S3_TARGET S3_FAR *target)
{
    const S3_PDEV S3_FAR *pdev = (const S3_PDEV S3_FAR *)device;
    const S3_BITMAP16 S3_FAR *bitmap = (const S3_BITMAP16 S3_FAR *)device;
    S3_SURFACE S3_FAR *surface;
    S3_U32 bytes;

    if (device == (void S3_FAR *)0 || target == (S3_TARGET S3_FAR *)0) {
        return 0;
    }
    if (pdev->pdType == S3_PDEV_DISPLAY) {
        surface = (S3_SURFACE S3_FAR *)pdev->surface_state;
        if (surface == (S3_SURFACE S3_FAR *)0) return 0;
        target->kind = S3_TARGET_SCREEN;
        target->width = surface->width;
        target->height = surface->height;
        target->stride = surface->pitch;
        target->surface = surface;
        target->bits = (S3_U8 S3_FAR *)0;
        return 1;
    }
    if (bitmap->bmType != 0 || bitmap->bmWidth <= 0 || bitmap->bmHeight <= 0 ||
        bitmap->bmWidthBytes <= 0 || bitmap->bmPlanes != 1 ||
        bitmap->bmBits == (S3_U8 S3_FAR *)0 || bitmap->bmSegmentIndex != 0) {
        return 0;
    }
    bytes = (S3_U32)(S3_U16)bitmap->bmWidthBytes * (S3_U32)(S3_U16)bitmap->bmHeight;
    if (bytes > 65536UL) return 0;
#if !defined(S3_HOST_TEST)
    if (!S3_FarSpanFitsSingleSegment((S3_U16)FP_OFF(bitmap->bmBits), bytes)) {
        return 0;
    }
#endif
    if (bitmap->bmBitsPixel == 8 && bitmap->bmWidthBytes >= bitmap->bmWidth) {
        target->kind = S3_TARGET_MEM8;
    } else if (bitmap->bmBitsPixel == 1 &&
               (S3_U32)(S3_U16)bitmap->bmWidthBytes * 8UL >=
               (S3_U32)(S3_U16)bitmap->bmWidth) {
        target->kind = S3_TARGET_MEM1;
    } else {
        return 0;
    }
    target->width = (S3_U16)bitmap->bmWidth;
    target->height = (S3_U16)bitmap->bmHeight;
    target->stride = (S3_U16)bitmap->bmWidthBytes;
    target->surface = (S3_SURFACE S3_FAR *)0;
    target->bits = bitmap->bmBits;
    return 1;
}

int S3_TargetReadRow(const S3_TARGET S3_FAR *target, S3_I16 x, S3_I16 y,
                     S3_U16 count, S3_U8 S3_FAR *out)
{
    S3_U8 S3_FAR *span;
    S3_U16 contiguous, run, i, column;

    switch (target->kind) {
    case S3_TARGET_SCREEN:
        while (count != 0) {
            span = S3_SurfaceSpan(target->surface, x, y, &contiguous);
            if (span == (S3_U8 S3_FAR *)0) return 0;
            run = contiguous < count ? contiguous : count;
            s3_fmemcpy(out, span, run);
            out += run;
            x = (S3_I16)(x + run);
            count = (S3_U16)(count - run);
        }
        return 1;
    case S3_TARGET_MEM8:
        s3_fmemcpy(out, target->bits + (S3_U16)y * target->stride + (S3_U16)x, count);
        return 1;
    case S3_TARGET_MEM1:
        for (i = 0; i < count; ++i) {
            column = (S3_U16)(x + i);
            out[i] = (target->bits[(S3_U16)y * target->stride + (column >> 3)] &
                      (0x80U >> (column & 7))) ? 0xFF : 0x00;
        }
        return 1;
    default:
        return 0;
    }
}

int S3_TargetWriteRow(const S3_TARGET S3_FAR *target, S3_I16 x, S3_I16 y,
                      S3_U16 count, const S3_U8 S3_FAR *in)
{
    S3_U8 S3_FAR *span;
    S3_U8 S3_FAR *byte;
    S3_U16 contiguous, run, i, column;
    S3_U8 bit;

    switch (target->kind) {
    case S3_TARGET_SCREEN:
        while (count != 0) {
            span = S3_SurfaceSpan(target->surface, x, y, &contiguous);
            if (span == (S3_U8 S3_FAR *)0) return 0;
            run = contiguous < count ? contiguous : count;
            s3_fmemcpy(span, in, run);
            in += run;
            x = (S3_I16)(x + run);
            count = (S3_U16)(count - run);
        }
        return 1;
    case S3_TARGET_MEM8:
        s3_fmemcpy(target->bits + (S3_U16)y * target->stride + (S3_U16)x, in, count);
        return 1;
    case S3_TARGET_MEM1:
        for (i = 0; i < count; ++i) {
            column = (S3_U16)(x + i);
            byte = target->bits + (S3_U16)y * target->stride + (column >> 3);
            bit = (S3_U8)(0x80U >> (column & 7));
            if (in[i] & 0x80) {
                *byte = (S3_U8)(*byte | bit);
            } else {
                *byte = (S3_U8)(*byte & (S3_U8)~bit);
            }
        }
        return 1;
    default:
        return 0;
    }
}

S3_U8 S3_Rop3(S3_U8 rop, S3_U8 pattern, S3_U8 source, S3_U8 destination)
{
    S3_U8 p = pattern, s = source, d = destination, result = 0;
    S3_U8 np = (S3_U8)~p, ns = (S3_U8)~s, nd = (S3_U8)~d;

    if (rop & 0x01) result |= (S3_U8)(np & ns & nd);
    if (rop & 0x02) result |= (S3_U8)(np & ns & d);
    if (rop & 0x04) result |= (S3_U8)(np & s & nd);
    if (rop & 0x08) result |= (S3_U8)(np & s & d);
    if (rop & 0x10) result |= (S3_U8)(p & ns & nd);
    if (rop & 0x20) result |= (S3_U8)(p & ns & d);
    if (rop & 0x40) result |= (S3_U8)(p & s & nd);
    if (rop & 0x80) result |= (S3_U8)(p & s & d);
    return result;
}

/* 3:3:2 index to monochrome: white when the colour is at least half bright. */
static S3_U8 s3_index_to_mono(S3_U8 index)
{
    S3_U16 red = (S3_U16)((index >> 5) & 7);
    S3_U16 green = (S3_U16)((index >> 2) & 7);
    S3_U16 blue = (S3_U16)(index & 3);
    /* Weights 30/59/11 scaled to each channel's range (7, 7, 3). */
    S3_U16 luma = (S3_U16)(red * 300U / 7U + green * 590U / 7U + blue * 110U / 3U);
    return (S3_U8)(luma >= 500U ? 0xFF : 0x00);
}

static int s3_clip(S3_I16 S3_FAR *dx, S3_I16 S3_FAR *dy,
                   S3_I16 S3_FAR *sx, S3_I16 S3_FAR *sy,
                   S3_I16 S3_FAR *width, S3_I16 S3_FAR *height,
                   const S3_TARGET S3_FAR *destination,
                   const S3_TARGET S3_FAR *source)
{
    if (*dx < 0) { *width = (S3_I16)(*width + *dx); *sx = (S3_I16)(*sx - *dx); *dx = 0; }
    if (*dy < 0) { *height = (S3_I16)(*height + *dy); *sy = (S3_I16)(*sy - *dy); *dy = 0; }
    if (source != (const S3_TARGET S3_FAR *)0) {
        if (*sx < 0) { *width = (S3_I16)(*width + *sx); *dx = (S3_I16)(*dx - *sx); *sx = 0; }
        if (*sy < 0) { *height = (S3_I16)(*height + *sy); *dy = (S3_I16)(*dy - *sy); *sy = 0; }
        if ((S3_I32)*sx + *width > (S3_I32)source->width)
            *width = (S3_I16)(source->width - (S3_U16)*sx);
        if ((S3_I32)*sy + *height > (S3_I32)source->height)
            *height = (S3_I16)(source->height - (S3_U16)*sy);
    }
    if ((S3_I32)*dx + *width > (S3_I32)destination->width)
        *width = (S3_I16)(destination->width - (S3_U16)*dx);
    if ((S3_I32)*dy + *height > (S3_I32)destination->height)
        *height = (S3_I16)(destination->height - (S3_U16)*dy);
    return *width > 0 && *height > 0;
}

/* Solid PATCOPY on the screen: fill spans directly. */
static int s3_fill_screen(const S3_TARGET S3_FAR *target, S3_I16 x, S3_I16 y,
                          S3_I16 width, S3_I16 height, S3_U8 color)
{
    S3_I16 row;
    S3_U16 count, contiguous, run;
    S3_I16 column;
    S3_U8 S3_FAR *span;

    for (row = 0; row < height; ++row) {
        column = x;
        count = (S3_U16)width;
        while (count != 0) {
            span = S3_SurfaceSpan(target->surface, column, (S3_I16)(y + row), &contiguous);
            if (span == (S3_U8 S3_FAR *)0) return 0;
            run = contiguous < count ? contiguous : count;
            s3_fmemset(span, color, run);
            column = (S3_I16)(column + run);
            count = (S3_U16)(count - run);
        }
    }
    return 1;
}

int S3_RasterBitBlt(void S3_FAR *destination, S3_I16 dx, S3_I16 dy,
                    void S3_FAR *source, S3_I16 sx, S3_I16 sy,
                    S3_I16 width, S3_I16 height, S3_U32 rop3,
                    const S3_PBRUSH16 S3_FAR *brush,
                    const S3_DRAWMODE16 S3_FAR *mode)
{
    S3_TARGET dst, src;
    S3_U8 rop = (S3_U8)(rop3 >> 16);
    int use_p = (((rop >> 4) ^ rop) & 0x0F) != 0;
    int use_s = (((rop >> 2) ^ rop) & 0x33) != 0;
    int use_d = (((rop >> 1) ^ rop) & 0x55) != 0;
    S3_U8 background = 0xFF, foreground = 0x00;
    S3_I16 row, first_row, step, i, chunk_x, chunk;
    S3_I16 ty, sy_row;
    S3_U8 p, s;

    if (!S3_BindTarget(destination, &dst)) return 0;
    if (use_s && !S3_BindTarget(source, &src)) return 0;
    S3_TRACE_EVENT("blt.enter", rop, (dst.kind << 8) | (use_s ? src.kind : 0));
    S3_TRACE_EVENT("blt.dst", dst.stride, dst.height);
    S3_TRACE_EVENT("blt.rect", (S3_U16)dx, (S3_U16)dy);
    S3_TRACE_EVENT("blt.size", (S3_U16)width, (S3_U16)height);
    if (!s3_clip(&dx, &dy, &sx, &sy, &width, &height, &dst,
                 use_s ? &src : (S3_TARGET S3_FAR *)0)) {
        return 1; /* fully clipped */
    }
    if (use_p) {
        if (brush == (const S3_PBRUSH16 S3_FAR *)0) return 0;
        if (brush->style == S3_BS_HOLLOW) return 1;
    }
    if (mode != (const S3_DRAWMODE16 S3_FAR *)0) {
        background = (S3_U8)mode->bkColor;
        foreground = (S3_U8)mode->TextColor;
    }

    /* Constant results and solid pattern fills avoid the per-pixel path. */
    if (!use_s && !use_d && dst.kind == S3_TARGET_SCREEN &&
        (!use_p || brush->style == S3_BS_SOLID)) {
        p = use_p ? brush->color[0] : 0;
        return s3_fill_screen(&dst, dx, dy, width, height, S3_Rop3(rop, p, 0, 0));
    }

    /* Overlapping copies within one target must read rows before they are
       overwritten: walk bottom-up when the source lies above. Each source
       row is buffered before writing, which handles horizontal overlap. */
    first_row = 0;
    step = 1;
    if (use_s && src.kind == dst.kind && src.bits == dst.bits &&
        src.surface == dst.surface && sy < dy) {
        first_row = (S3_I16)(height - 1);
        step = -1;
    }

    for (chunk_x = 0; chunk_x < width; chunk_x = (S3_I16)(chunk_x + S3_ROW_MAX)) {
        chunk = (S3_I16)(width - chunk_x);
        if (chunk > S3_ROW_MAX) chunk = S3_ROW_MAX;
        for (row = first_row; row >= 0 && row < height; row = (S3_I16)(row + step)) {
            ty = (S3_I16)(dy + row);
            if (use_s) {
                sy_row = (S3_I16)(sy + row);
                if (!S3_TargetReadRow(&src, (S3_I16)(sx + chunk_x), sy_row,
                                      (S3_U16)chunk, s3_src_row)) return 0;
                if (src.kind == S3_TARGET_MEM1 && dst.kind != S3_TARGET_MEM1) {
                    for (i = 0; i < chunk; ++i)
                        s3_src_row[i] = s3_src_row[i] ? background : foreground;
                } else if (src.kind != S3_TARGET_MEM1 && dst.kind == S3_TARGET_MEM1) {
                    for (i = 0; i < chunk; ++i)
                        s3_src_row[i] = (S3_U8)(s3_src_row[i] == background ? 0xFF : 0x00);
                }
            }
            if (use_d) {
                if (!S3_TargetReadRow(&dst, (S3_I16)(dx + chunk_x), ty,
                                      (S3_U16)chunk, s3_dst_row)) return 0;
            }
            for (i = 0; i < chunk; ++i) {
                p = 0;
                if (use_p) {
                    p = brush->color[(((S3_U16)ty & 7) << 3) |
                                     ((S3_U16)(dx + chunk_x + i) & 7)];
                    if (dst.kind == S3_TARGET_MEM1) p = s3_index_to_mono(p);
                }
                s = use_s ? s3_src_row[i] : 0;
                s3_dst_row[i] = S3_Rop3(rop, p, s, use_d ? s3_dst_row[i] : 0);
            }
            if (!S3_TargetWriteRow(&dst, (S3_I16)(dx + chunk_x), ty,
                                   (S3_U16)chunk, s3_dst_row)) return 0;
        }
    }
    return 1;
}
